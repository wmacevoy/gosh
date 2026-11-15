/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef GOSH_FORWARDING_CONNECTION_HPP
#define GOSH_FORWARDING_CONNECTION_HPP

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "src/forwarding/tcp/bytes.h"
#include "src/forwarding/tcp/early.h"
#include "src/forwarding/tcp/held.h"
#include "src/forwarding/tcp/options.h"
#include "src/forwarding/tcp/tcpinudp.h"
#include "src/forwarding/tcp/timer.h"

/*
 * forwarding: connection (design v2, 2.2). gosh's TCP: one end of each forwarded connection
 * (REQ1/FWD-1), which outlives any loss of contact (REQ1/FWD-3).
 *
 * The kernel gives an unprivileged program byte streams, not TCP segments. So each end of a
 * forward has a TCP of its own, enough of one to be a correct peer for the one at the other end:
 *
 *   application <-- kernel TCP --> socket | this <== segments, over the side channel ==> other end
 *
 * What it does: the handshake in both roles, cumulative acknowledgment, reassembly of segments
 * that arrive out of order, retransmission with an adaptive, backed-off timeout (RFC 6298), fast
 * retransmit and recovery (NewReno, RFC 6582), slow start and congestion avoidance, flow control
 * both ways including zero-window probes, window scaling, half-close of each direction, resets;
 * and selective acknowledgment (RFC 2018), where both ends ask for it: the receiver says which
 * ranges it holds beyond a gap, and the sender sends again only what none of them covers.
 * Without it (the fork has none) a burst that overran the receiver was repaired by sending again
 * most of what had arrived: three datagrams for each one needed (docs/FINDINGS.md, 2026-11-14).
 * While the session is out of contact, unanswered retransmissions are not held against a
 * connection, and when contact returns everything outstanding is sent again at once.
 *
 * THIS IS LOGIC. It makes no system call, reads no clock and draws no random number. It is told
 * what happened, with the time (`now`, in milliseconds, from any fixed start) and what the session
 * can do just now (`Link`), and it says what to do through `Out`, a type the caller supplies:
 * the edge in the programs (tcpadapter.cc), a recorder in tests. `Out` is a template parameter
 * and not an interface, so that the programs pay nothing for the tests' sake.
 *
 * What Out must have. Each is called from inside the calls below, and answers at once:
 *
 *   void     send( uint8_t forward, const Network::TCPInUDP& segment )
 *              A segment for the other end.
 *   uint32_t fresh_sequence()
 *              A number nobody could guess, for a new connection's first sequence number.
 *   bool     connect( Id id, uint8_t forward )
 *              The connecting end: begin reaching this forward's target for connection `id`.
 *              False if there is no such target, or not even a socket to try with; then the
 *              other end is refused.
 *   int      connecting( Id id )
 *              How that is going: 0 not known yet, 1 reached, -1 refused.
 *   long     deliver( Id id, const char* data, size_t length )
 *              Bytes for the application at this end. Returns how many it took (0: none just
 *              now, try later), or -1 if the application has gone.
 *   void     shut_write( Id id )
 *              The other end has finished sending: no more will be delivered.
 *   void     close( Id id, bool reset )
 *              The connection is over; `reset` if the application should see a reset and not
 *              an orderly end. After this the id means nothing, and may be used again.
 *
 * How it fails. A segment for no connection is answered with a reset. A segment that makes no
 * sense for its connection is ignored. A reset is believed only if it is where the other end
 * should be. An application that has gone, or a target that refuses or does not answer in
 * CONNECT_TIMEOUT, ends the connection and tells the other end. Unanswered retransmissions end
 * it after MAX_RETRIES, but only while the session is in contact.
 */
namespace Gosh {

/* What the session can do at the moment of a call. The edge reads these; they are not kept. */
struct Link
{
  bool ready;          /* the other end forwards, and a segment can be sent now */
  bool in_contact;     /* the other end has been heard from lately */
  size_t max_datagram; /* the largest segment, in datagram form, that can be sent */
};

template<class Out>
class Connections
{
public:
  /* Names a connection: its forward, and the listening end's port for it, which is what names it on the wire. */
  typedef uint32_t Id;
  static Id id_of( uint8_t forward, uint16_t port ) { return ( static_cast<uint32_t>( forward ) << 16 ) | port; }

  static constexpr size_t MAX_CONNECTIONS = 64;
  static constexpr size_t SEND_BUFFER = 1024 * 1024;
  static constexpr size_t RECV_BUFFER = 1024 * 1024;
  static constexpr size_t READ_AT_ONCE = 65536; /* the most asked of a socket in one read */
  /*
   * A TCP header's window field counts to 65535. With the window scale option (RFC 7323) it
   * counts in units of this many bits' worth of bytes, if both ends ask for that, so that more
   * than 64 KB can be on its way at once: without it a long path is a slow one.
   */
  static constexpr uint8_t WINDOW_SHIFT = 5;

  static constexpr uint32_t DEFAULT_MSS = 536;
  static constexpr uint32_t MIN_MSS = 64; /* the least we will send at a time, whatever the other end asks */
  static constexpr uint32_t INITIAL_WINDOW_SEGMENTS = 10;

  static constexpr uint32_t RTO_INITIAL = RoundTrip::RTO_INITIAL; /* (timer.h) */
  static constexpr uint32_t RTO_MAX = RoundTrip::RTO_MAX;
  static constexpr unsigned int MAX_RETRIES = 10;
  static constexpr unsigned int MAX_SYN_RETRIES = 5;
  /*
   * Lost segments are repaired one per round trip, which is right for the odd one and hopeless
   * for hundreds (a burst that overran the path). After this many in one repair, stop: go back
   * to the first of them and send on from there, slowly at first.
   */
  static constexpr unsigned int MANY_HOLES = 3;
  static constexpr uint64_t CONNECT_TIMEOUT = 10000;
  static constexpr int POLL_INTERVAL = 10; /* ms, while waiting on a target or on a slow application */

private:
  typedef Network::TCPInUDP Segment;

  enum State
  {
    CONNECTING,  /* connecting end: waiting to reach the target */
    SYN_SENT,    /* listening end: waiting for the other end to accept */
    SYN_RCVD,    /* connecting end: accepted; waiting for the handshake to finish */
    ESTABLISHED, /* including after either direction has closed */
  };

  struct Conn
  {
    uint8_t forward;
    uint16_t port; /* the listening end's port: names this connection in the tunnel */
    State state;
    bool dead; /* finished with; to be removed */

    uint32_t mss; /* largest payload we send */

    /* sending: bytes read from the socket, on their way to the other end */
    Bytes send_buf; /* everything from snd_una on, sent or not */
    uint32_t iss;
    uint32_t snd_una; /* oldest unacknowledged */
    uint32_t snd_nxt; /* next to send (falls back to snd_una on a timeout) */
    uint32_t snd_max; /* highest ever sent, plus one */
    uint32_t snd_wnd; /* what the other end will accept */
    uint8_t snd_scale; /* how far to shift the windows it tells us... */
    uint8_t rcv_scale; /* ...and the ones we tell it (both 0 unless both asked) */
    uint32_t cwnd, ssthresh;
    unsigned int dupacks;
    bool recovering;     /* repairing a loss found by duplicate acknowledgments... */
    uint32_t recover;    /* ...which is over when this much is acknowledged */
    unsigned int holes;  /* how many more losses that repair has uncovered */
    bool resending;      /* sending again from an earlier point, after heavy loss or a timeout, up to recover */
    uint64_t calm_until; /* ...and for a moment after: its echoes are still coming back */
    bool local_eof;      /* the socket has no more to send */
    bool fin_sent;       /* our FIN lies before snd_nxt */
    bool fin_was_sent;   /* ...or did once, at fin_seq */
    bool fin_acked;
    uint32_t fin_seq;

    /* retransmission */
    uint64_t rto_deadline; /* 0 when the timer is off */
    uint32_t rto;
    unsigned int retries;
    bool rtt_timing;
    uint32_t rtt_seq;
    uint64_t rtt_start;
    RoundTrip round_trip; /* (timer.h) */

    /* receiving: bytes from the other end, on their way to the socket */
    Bytes recv_buf;
    uint32_t rcv_nxt;
    Early early; /* segments from beyond rcv_nxt, and what to say we hold (early.h) */
    bool fin_received;
    bool peer_gone;       /* after its finish, the other end said it has forgotten us (a reset) */
    bool shut_wr;         /* we have passed their FIN on to the socket */
    uint32_t last_window; /* what we last said we would accept, in bytes */

    uint64_t connect_deadline;

    /* selective acknowledgment, when both ends asked for it in the handshake */
    bool sack;
    Held held;           /* sending: what the other end says it holds beyond snd_una (held.h) */
    uint32_t rexmit_nxt; /* sending, while repairing a loss: where to look next for a gap to send again */

    Conn()
      : forward( 0 ), port( 0 ), state( CONNECTING ), dead( false ), mss( DEFAULT_MSS ), send_buf(), iss( 0 ),
        snd_una( 0 ), snd_nxt( 0 ), snd_max( 0 ), snd_wnd( 0 ), snd_scale( 0 ), rcv_scale( 0 ), cwnd( 0 ),
        ssthresh( 0xFFFFFFFF ), dupacks( 0 ), recovering( false ), recover( 0 ), holes( 0 ), resending( false ),
        calm_until( 0 ), local_eof( false ), fin_sent( false ), fin_was_sent( false ), fin_acked( false ), fin_seq( 0 ),
        rto_deadline( 0 ), rto( RTO_INITIAL ), retries( 0 ), rtt_timing( false ), rtt_seq( 0 ), rtt_start( 0 ),
        round_trip(), recv_buf(), rcv_nxt( 0 ), early(), fin_received( false ), peer_gone( false ), shut_wr( false ), last_window( 0 ),
        connect_deadline( 0 ), sack( false ), held(), rexmit_nxt( 0 )
    {}

    Id id( void ) const { return id_of( forward, port ); }
  };

  bool listening; /* this end listens for applications (the client, for -L); the other connects to targets */
  bool offer_sack; /* ask for selective acknowledgment (and agree to it when asked) */
  std::list<Conn> conns;
  uint16_t next_port;
  bool link_was_up;
  Link link; /* as given to the call now being made */

  /* sequence numbers wrap */
  static bool seq_lt( uint32_t a, uint32_t b ) { return static_cast<int32_t>( a - b ) < 0; }

  /*
   * Repairing a loss: sends again the next gap, from rexmit_nxt on, that lies below something
   * the other end holds; one segment of it. False if there is no such gap: what is beyond the
   * last thing it holds may simply not have arrived yet.
   */
  bool sack_retransmit( Conn& c, Out& out )
  {
    uint32_t at = 0, gap_end = 0;
    if ( !c.held.next_gap( c.snd_una, c.rexmit_nxt, at, gap_end ) || at >= c.send_buf.size() ) {
      return false;
    }
    const size_t n = std::min<size_t>( std::min<size_t>( std::min( c.mss, local_mss() ), gap_end - at ),
                                       c.send_buf.size() - at );
    emit( c, Segment::ACK, c.snd_una + at, c.send_buf.substr( at, n ), out );
    c.rexmit_nxt = c.snd_una + at + n;
    return true;
  }

  Conn* find( uint8_t forward, uint16_t port )
  {
    for ( typename std::list<Conn>::iterator i = conns.begin(); i != conns.end(); i++ ) {
      if ( !i->dead && i->forward == forward && i->port == port ) {
        return &*i;
      }
    }
    return NULL;
  }

  Conn* find( Id id ) { return find( static_cast<uint8_t>( id >> 16 ), static_cast<uint16_t>( id & 0xFFFF ) ); }

  const Conn* find( Id id ) const { return const_cast<Connections*>( this )->find( id ); }

  Conn* new_conn( uint8_t forward, uint16_t port, Out& out )
  {
    conns.push_back( Conn() );
    Conn& c = conns.back();
    c.forward = forward;
    c.port = port;
    c.iss = out.fresh_sequence();
    c.snd_una = c.snd_nxt = c.snd_max = c.iss;
    return &c;
  }

  void sweep( void )
  {
    for ( typename std::list<Conn>::iterator i = conns.begin(); i != conns.end(); ) {
      if ( i->dead ) {
        i = conns.erase( i );
      } else {
        i++;
      }
    }
  }

  uint32_t local_mss( void ) const
  {
    /* We send no options after the handshake, so a segment is our 12 bytes of header and its data. */
    const size_t limit = link.max_datagram;
    if ( limit < Segment::DATAGRAM_HEADER_LEN + MIN_MSS ) {
      return MIN_MSS;
    }
    return std::min<size_t>( limit - Segment::DATAGRAM_HEADER_LEN, 0xFFFF );
  }

  /*
   * How much to send at a time: what the other end asked for, if it fits. An absurdly small
   * request is not honoured: a peer that asked for segments of no bytes at all would otherwise
   * have us send for ever.
   */
  uint32_t agreed_mss( const std::string& their_options ) const
  {
    return std::max<uint32_t>( MIN_MSS, std::min( SegmentOptions::mss( their_options, DEFAULT_MSS ), local_mss() ) );
  }

  /* How much we can take, in bytes, as nearly as the header can say it. */
  uint32_t window( const Conn& c ) const
  {
    const size_t space = c.recv_buf.size() < RECV_BUFFER ? RECV_BUFFER - c.recv_buf.size() : 0;
    return std::min<size_t>( space >> c.rcv_scale, 0xFFFF ) << c.rcv_scale;
  }

  void emit( Conn& c, uint16_t flags, uint32_t seq, const std::string& payload, Out& out, bool with_options = false )
  {
    Segment s;
    ( listening ? s.source_port : s.dest_port ) = c.port;
    s.flags = flags;
    s.seq = seq;
    s.ack = ( flags & Segment::ACK ) ? c.rcv_nxt : 0;
    if ( flags & Segment::SYN ) {
      /* (the window in a SYN is never scaled: the other end does not know the scale until it has read it) */
      c.last_window = std::min<uint32_t>( window( c ), 0xFFFF );
      s.window = c.last_window;
    } else {
      c.last_window = window( c );
      s.window = c.last_window >> c.rcv_scale;
    }
    if ( with_options ) {
      const uint32_t m = local_mss();
      const char mss[4] = { 2, 4, static_cast<char>( m >> 8 ), static_cast<char>( m & 0xFF ) };
      s.options.assign( mss, sizeof( mss ) );
      /* The listening end asks for window scaling; the connecting end agrees if it was asked. */
      if ( listening || c.rcv_scale ) {
        const char scale[4] = { 1, 3, 3, static_cast<char>( WINDOW_SHIFT ) };
        s.options.append( scale, sizeof( scale ) );
      }
      /* Likewise selective acknowledgment: the listening end asks, the connecting end agrees if asked. */
      if ( offer_sack && ( listening || c.sack ) ) {
        const char permitted[4] = { 1, 1, 4, 2 };
        s.options.append( permitted, sizeof( permitted ) );
      }
    } else if ( c.sack && payload.empty() && flags == Segment::ACK && !c.early.empty() ) {
      /* an ACK with nothing else to say, while we hold something out of order: say what */
      const size_t room = link.max_datagram > Segment::DATAGRAM_HEADER_LEN + 4
                            ? ( link.max_datagram - Segment::DATAGRAM_HEADER_LEN - 4 ) / 8
                            : 0;
      s.options = c.early.blocks( c.rcv_nxt, RECV_BUFFER, room );
    }
    s.payload = payload;
    out.send( c.forward, s );
  }

  /* Answer a segment that belongs to no connection, as RFC 9293 3.10.7.1 says to. */
  void reply_reset( uint8_t forward, uint16_t port, const Segment& to, Out& out )
  {
    if ( to.has( Segment::RST ) ) {
      return;
    }

    Segment s;
    ( listening ? s.source_port : s.dest_port ) = port;
    if ( to.has( Segment::ACK ) ) {
      s.flags = Segment::RST;
      s.seq = to.ack;
    } else {
      s.flags = Segment::RST | Segment::ACK;
      s.ack = to.seq + to.payload.size() + ( to.has( Segment::SYN ) ? 1 : 0 ) + ( to.has( Segment::FIN ) ? 1 : 0 );
    }
    out.send( forward, s );
  }

  void abort( Conn& c, bool tell_peer, Out& out )
  {
    if ( c.dead ) {
      return;
    }
    if ( tell_peer && c.state != CONNECTING && !c.peer_gone ) { /* (there is nobody to tell, if it has gone) */
      emit( c, Segment::RST | Segment::ACK, c.snd_nxt, "", out );
    }
    out.close( c.id(), true );
    c.dead = true;
  }

  void maybe_finish( Conn& c, Out& out )
  {
    /* both directions closed and everything delivered */
    if ( !c.dead && c.fin_acked && c.fin_received && c.recv_buf.empty() ) {
      out.close( c.id(), false );
      c.dead = true;
    }
  }

  /*
   * A reset from an end whose finish we already have. Everything it had to send arrived, whole
   * and in order; the reset says only that it has since forgotten the connection (it closed
   * when both finishes were acknowledged, and something of ours reached it afterwards: word
   * that our window had opened, our finish sent again because its acknowledgment was lost, or
   * a segment late or twice). The fork ended the connection here and threw away what the
   * application had not yet taken: up to 1 MB that had been acknowledged (docs/FINDINGS.md,
   * 2026-11-14; queue item 18).
   *
   * So: what we hold is still delivered, and then the connection ends, in order (unless the
   * application itself goes first: then it ends as any connection does whose application has
   * gone, and there is no one left to mind how). From here on
   * the connection has no other end: nothing is sent to it, nothing it sent earlier that
   * arrives late is looked at, no timer runs, and what the application still writes is taken
   * from it and dropped, so that it is not left stuck writing to nobody.
   *
   * In order, and not with a reset, even if some of ours was never acknowledged. We cannot
   * tell whether that was lost or only its acknowledgment was; and a reset given to the
   * application's socket discards what it has not yet read, which is the loss this prevents.
   * An application whose last words did not arrive learns as it does from any TCP: by what it
   * does not get back, or when it next writes.
   */
  void forgotten( Conn& c, Out& out )
  {
    if ( c.dead || c.peer_gone ) {
      return;
    }
    c.peer_gone = true;
    c.rto_deadline = 0;
    c.send_buf.drop( c.send_buf.size() );
    c.fin_acked = true; /* (as far as it ever will be: this lets the connection finish when all is delivered) */
    maybe_finish( c, out ); /* (what is held goes to the application at the next tick, as always) */
  }

  /* Connecting end: the other end wants a connection. See whether the target will have it. */
  void passive_open( uint8_t forward, uint16_t port, const Segment& syn, uint64_t now, Out& out )
  {
    if ( conns.size() >= MAX_CONNECTIONS || !out.connect( id_of( forward, port ), forward ) ) {
      reply_reset( forward, port, syn, out );
      return;
    }
    /* (a target that is reached at once is found to be so on the next tick, like any other) */

    Conn* c = new_conn( forward, port, out );
    c->state = CONNECTING;
    c->connect_deadline = now + CONNECT_TIMEOUT;
    c->rcv_nxt = syn.seq + 1;
    c->snd_wnd = syn.window;
    c->mss = agreed_mss( syn.options );
    c->sack = offer_sack && SegmentOptions::sack_permitted( syn.options );
    if ( SegmentOptions::scale( syn.options ) >= 0 ) {
      c->snd_scale = SegmentOptions::scale( syn.options );
      c->rcv_scale = WINDOW_SHIFT;
    }
  }

  /* Connecting end: the target has been reached, so accept the connection from the tunnel. */
  void connected( Conn& c, uint64_t now, Out& out )
  {
    c.state = SYN_RCVD;
    emit( c, Segment::SYN | Segment::ACK, c.iss, "", out, true );
    c.snd_nxt = c.snd_max = c.iss + 1;
    c.rto_deadline = now + c.rto;
  }

  void established_input( Conn& c, const Segment& segment, uint64_t now, Out& out )
  {
    if ( segment.has( Segment::RST ) ) {
      /* believe it only if it is where we expect the other end to be */
      if ( segment.seq - c.rcv_nxt <= 0xFFFF ) {
        if ( c.fin_received ) {
          forgotten( c, out );
        } else {
          abort( c, false, out );
        }
      }
      return;
    }
    if ( c.peer_gone ) {
      return; /* (something it sent before it forgot us: there is nobody to answer, and nothing to learn) */
    }
    if ( segment.has( Segment::SYN ) ) {
      emit( c, Segment::ACK, c.snd_nxt, "", out ); /* our handshake ACK was lost */
      return;
    }

    if ( segment.has( Segment::ACK ) ) {
      process_ack( c, segment, now, out );
    }

    if ( !segment.payload.empty() || segment.has( Segment::FIN ) ) {
      accept_data( c, segment.seq, segment.payload, segment.has( Segment::FIN ) );

      flush( c, out );
      if ( c.dead ) {
        return;
      }
      emit( c, Segment::ACK, c.snd_nxt, "", out );
    }

    output( c, now, out );
    maybe_finish( c, out );
  }

  /* Take what continues the stream, and keep what will continue it later. */
  void accept_data( Conn& c, uint32_t seq, const std::string& payload, bool fin )
  {
    if ( c.fin_received ) {
      return;
    }

    const uint32_t skip = c.rcv_nxt - seq; /* how much of this we already have */
    if ( skip > payload.size() ) {
      /* Not next. If it is ahead of us (and not absurdly far), hold on to it. */
      c.early.keep( seq, payload, c.rcv_nxt, RECV_BUFFER );
      return; /* (an early FIN is not remembered; it will be sent again) */
    }

    const size_t space = c.recv_buf.size() < RECV_BUFFER ? RECV_BUFFER - c.recv_buf.size() : 0;
    const size_t take = std::min( payload.size() - skip, space );
    c.recv_buf.append( payload, skip, take );
    c.rcv_nxt += take;
    if ( take < payload.size() - skip ) {
      return; /* no room for the rest */
    }
    if ( fin ) {
      c.fin_received = true;
      c.rcv_nxt++;
      c.early.clear();
      return;
    }

    /* Does something we were holding come next now? */
    uint32_t held_seq = 0;
    std::string held;
    if ( c.early.take_next( c.rcv_nxt, RECV_BUFFER, held_seq, held ) ) {
      accept_data( c, held_seq, held, false ); /* which looks for what comes after that */
    }
  }

  void process_ack( Conn& c, const Segment& segment, uint64_t now, Out& out )
  {
    const uint32_t ack = segment.ack;

    if ( seq_lt( c.snd_max, ack ) || seq_lt( ack, c.snd_una ) ) {
      return; /* for something we never sent, or old news */
    }

    const uint32_t flight = c.snd_max - c.snd_una;
    const uint32_t their_window = static_cast<uint32_t>( segment.window ) << c.snd_scale;

    if ( c.sack ) {
      /* (the data outstanding: not a FIN, which takes a sequence number and is no bytes of ours to pass over) */
      c.held.note( segment.options, c.snd_una, std::min<uint32_t>( c.snd_max - c.snd_una, c.send_buf.size() ) );
    }

    if ( ack == c.snd_una ) {
      /* Nothing new. Three of these in a row, otherwise empty, mean a segment went missing. */
      if ( flight && segment.payload.empty() && !segment.has( Segment::FIN ) && their_window == c.snd_wnd ) {
        /*
         * (While resending, and for a round trip after, duplicates are of our own making: the
         * other end already had some of what we sent again, and says so. To take that for loss
         * would be to slow down for nothing.)
         */
        if ( ++c.dupacks == 3 && !c.recovering && !c.resending && now >= c.calm_until ) {
          c.ssthresh = std::max( flight / 2, 2 * c.mss );
          c.cwnd = c.ssthresh;
          c.rtt_timing = false;
          c.recovering = true;
          c.holes = 0;
          c.recover = c.snd_max;
          c.rexmit_nxt = c.snd_una;
          if ( !c.sack || !sack_retransmit( c, out ) ) {
            retransmit_one( c, out );
            c.rexmit_nxt = c.snd_una + std::min<uint32_t>( std::min( c.mss, local_mss() ), c.send_buf.size() );
          }
        } else if ( c.sack && c.recovering ) {
          /* another segment has arrived there beyond the gap, so one has left the path: fill the next gap */
          sack_retransmit( c, out );
        }
      }
      c.snd_wnd = their_window;
      return;
    }

    /* new data acknowledged */
    uint32_t acked = ack - c.snd_una;
    if ( c.fin_was_sent && ack == c.fin_seq + 1 ) {
      c.fin_sent = c.fin_acked = true;
      acked--;
    }
    c.send_buf.drop( acked );
    c.snd_una = ack;
    if ( seq_lt( c.snd_nxt, ack ) ) {
      c.snd_nxt = ack;
    }
    c.held.forget( c.snd_una );
    c.snd_wnd = their_window;
    c.dupacks = 0;
    c.retries = 0;

    /* round-trip time, from a segment that was sent only once */
    if ( c.rtt_timing && !seq_lt( ack, c.rtt_seq ) ) {
      c.round_trip.sample( now - c.rtt_start );
      c.rtt_timing = false;
    }
    c.rto = c.round_trip.settled();

    if ( c.recovering ) {
      if ( seq_lt( ack, c.recover ) ) {
        if ( c.sack ) {
          /*
           * That filled one gap. With what the other end says it holds, the rest are known:
           * send the next one, or this new first one if it has not been sent again yet.
           */
          if ( !sack_retransmit( c, out ) && static_cast<int32_t>( c.rexmit_nxt - c.snd_una ) <= 0 ) {
            retransmit_one( c, out );
            c.rexmit_nxt = c.snd_una + std::min<uint32_t>( std::min( c.mss, local_mss() ), c.send_buf.size() );
          }
          c.rto_deadline = now + c.rto;
          return;
        }
        if ( ++c.holes < MANY_HOLES ) {
          /* That filled one hole and revealed another. Fill it too, without waiting to be asked three times. */
          retransmit_one( c, out );
          c.rto_deadline = now + c.rto;
          return;
        }
        /*
         * And another, and another: a great deal was lost, and one hole per round trip would
         * take all day. Send on from here instead, gently, as after a timeout but without
         * waiting for one. What the other end already has of it, it will say, and that is skipped.
         */
        c.cwnd = 2 * c.mss;
        c.snd_nxt = c.snd_una;
        c.fin_sent = c.fin_acked;
        c.recovering = false;
        c.resending = true;
        c.rto_deadline = now + c.rto;
        return;
      }
      c.recovering = false;
    }
    if ( c.resending && !seq_lt( ack, c.recover ) ) {
      c.resending = false;
      c.calm_until = now + c.rto;
    }

    /* slow start, then congestion avoidance */
    if ( c.cwnd < c.ssthresh ) {
      c.cwnd += std::min( acked, c.mss );
    } else {
      c.cwnd += std::max<uint32_t>( 1, static_cast<uint64_t>( c.mss ) * c.mss / c.cwnd );
    }
    c.cwnd = std::min<uint32_t>( c.cwnd, SEND_BUFFER );

    c.rto_deadline = ( c.snd_nxt != c.snd_una ) ? now + c.rto : 0;
  }

  /* Send whatever the windows allow. */
  void output( Conn& c, uint64_t now, Out& out )
  {
    if ( c.dead || c.state != ESTABLISHED || c.peer_gone ) {
      return;
    }

    /* What fits in a datagram can shrink while a connection is open, if mosh has to lower its own MTU. */
    const uint32_t mss = std::min( c.mss, local_mss() );

    while ( true ) {
      /* Sending again from an earlier point, pass over what the other end says it holds. */
      const uint32_t until_held = ( c.sack && !c.held.empty() && seq_lt( c.snd_nxt, c.snd_max ) ) ? c.held.pass_over( c.snd_una, c.snd_nxt )
                                                                                               : 0xFFFFFFFF;
      const uint32_t flight = c.snd_nxt - c.snd_una;
      /*
       * What it holds is not on its way any more, and does not count against how much may be
       * (cwnd). It does count against the room the other end gave (snd_wnd): that is where it is.
       */
      const uint32_t on_its_way = flight - c.held.below( c.snd_una, flight );
      const uint32_t data_sent = flight - ( c.fin_sent && !c.fin_acked ? 1 : 0 );
      const size_t unsent = c.send_buf.size() - data_sent;

      uint32_t allowed = std::min( c.snd_wnd, c.cwnd );
      uint32_t room = c.snd_wnd;
      if ( allowed == 0 && flight == 0 ) {
        allowed = room = 1; /* probe a closed window, or we could wait forever for it to open */
      }

      const bool fresh = !seq_lt( c.snd_nxt, c.snd_max );

      if ( unsent ) {
        if ( on_its_way >= allowed || flight >= room ) {
          break;
        }
        const size_t n = std::min<size_t>( std::min<size_t>( std::min<size_t>( unsent, mss ), allowed - on_its_way ),
                                           std::min<size_t>( until_held, room - flight ) );
        if ( n == 0 ) {
          break; /* (can't happen, but looping here for ever would be the worst way to find out) */
        }
        emit( c, Segment::ACK | ( n == unsent ? Segment::PSH : 0 ), c.snd_nxt, c.send_buf.substr( data_sent, n ), out );
        c.snd_nxt += n;
      } else if ( c.local_eof && !c.fin_sent ) {
        emit( c, Segment::FIN | Segment::ACK, c.snd_nxt, "", out );
        c.fin_sent = c.fin_was_sent = true;
        c.fin_seq = c.snd_nxt;
        c.snd_nxt++;
      } else {
        break;
      }

      if ( fresh && !c.rtt_timing ) {
        c.rtt_timing = true;
        c.rtt_seq = c.snd_nxt;
        c.rtt_start = now;
      }
      if ( seq_lt( c.snd_max, c.snd_nxt ) ) {
        c.snd_max = c.snd_nxt;
      }
      if ( !c.rto_deadline ) {
        c.rto_deadline = now + c.rto;
      }
    }
  }

  void retransmit_one( Conn& c, Out& out )
  {
    if ( !c.send_buf.empty() ) {
      emit( c, Segment::ACK, c.snd_una, c.send_buf.substr( 0, std::min( c.mss, local_mss() ) ), out );
    } else if ( c.fin_was_sent && !c.fin_acked ) {
      emit( c, Segment::FIN | Segment::ACK, c.fin_seq, "", out );
    }
  }

  /* The retransmission timer has gone off. */
  void timeout( Conn& c, uint64_t now, Out& out )
  {
    c.rto = std::min<uint32_t>( c.rto * 2, RTO_MAX );
    c.rto_deadline = now + c.rto;

    /*
     * Unanswered retransmissions count against a connection only while the session is in
     * contact with the other end. Otherwise the reason is plain, and no fault of the connection's.
     */
    const bool up = link.in_contact;

    if ( c.state == SYN_SENT || c.state == SYN_RCVD ) {
      if ( up && ++c.retries > MAX_SYN_RETRIES ) {
        abort( c, c.state == SYN_RCVD, out );
      } else if ( c.state == SYN_SENT ) {
        emit( c, Segment::SYN, c.iss, "", out, true );
      } else {
        emit( c, Segment::SYN | Segment::ACK, c.iss, "", out, true );
      }
      return;
    }

    /* A closed window is the other end's choice, not a sign that it has gone. */
    if ( up && c.snd_wnd != 0 && ++c.retries > MAX_RETRIES ) {
      abort( c, true, out );
      return;
    }

    /* Assume everything outstanding was lost, and start again gently from there. */
    c.ssthresh = std::max<uint32_t>( ( c.snd_max - c.snd_una ) / 2, 2 * c.mss );
    c.cwnd = c.mss;
    c.snd_nxt = c.snd_una;
    c.fin_sent = c.fin_acked;
    c.dupacks = 0;
    c.recovering = false;
    c.resending = true;
    c.recover = c.snd_max;
    c.rtt_timing = false;
    c.rto_deadline = 0;
    c.rexmit_nxt = c.snd_una;
    output( c, now, out );
    if ( !c.rto_deadline ) {
      c.rto_deadline = now + c.rto;
    }
  }

  /* Pass what has arrived on to the application. */
  void flush( Conn& c, Out& out )
  {
    if ( c.dead ) {
      return;
    }

    while ( !c.recv_buf.empty() ) {
      const long n = out.deliver( c.id(), c.recv_buf.data(), c.recv_buf.size() );
      if ( n > 0 ) {
        c.recv_buf.drop( n );
      } else if ( n == 0 ) {
        break; /* the application is slow; tick() will try again */
      } else {
        abort( c, true, out ); /* the application has gone */
        return;
      }
    }

    if ( c.recv_buf.empty() && c.fin_received && !c.shut_wr ) {
      out.shut_write( c.id() );
      c.shut_wr = true;
    }

    /* If we had told the other end to stop, tell it that it can go on. */
    if ( c.state == ESTABLISHED && !c.peer_gone && c.last_window < c.mss && window( c ) >= 2 * c.mss ) {
      emit( c, Segment::ACK, c.snd_nxt, "", out );
    }
  }

  /* unused */
  Connections( const Connections& );
  Connections& operator=( const Connections& );

public:
  /*
   * s_listening: this end listens for applications; the other end connects to targets.
   * s_offer_sack: ask for selective acknowledgment, and agree to it when asked. With false this
   * is the fork's TCP exactly, which is what an end without it (pf.7) gets either way.
   */
  explicit Connections( bool s_listening, bool s_offer_sack = true )
    : listening( s_listening ), offer_sack( s_offer_sack ), conns(), next_port( 32768 ), link_was_up( true ), link()
  {}

  /*
   * Listening end: an application has connected, or a stream has been opened. Opens a
   * connection for it through the tunnel and says its id. False, and nothing is opened, if the
   * other end cannot be sent to now or there are MAX_CONNECTIONS already: the caller then closes
   * what it had (with a reset, if an application was waiting on it).
   */
  bool opened( uint8_t forward, uint64_t now, const Link& s_link, Out& out, Id& id )
  {
    link = s_link;
    if ( !link.ready || conns.size() >= MAX_CONNECTIONS ) {
      return false;
    }

    while ( find( forward, next_port ) ) {
      next_port = next_port == 0xFFFF ? 32768 : next_port + 1;
    }
    Conn* c = new_conn( forward, next_port, out );
    next_port = next_port == 0xFFFF ? 32768 : next_port + 1;

    c->state = SYN_SENT;
    emit( *c, Segment::SYN, c->iss, "", out, true );
    c->snd_nxt = c->snd_max = c->iss + 1;
    c->rto_deadline = now + c->rto;
    id = c->id();
    return true;
  }

  /* A segment has arrived from the other end, on this forward. */
  void segment( uint8_t forward, const Segment& segment, uint64_t now, const Link& s_link, Out& out )
  {
    link = s_link;
    const uint16_t port = listening ? segment.dest_port : segment.source_port;

    Conn* c = find( forward, port );
    if ( !c ) {
      if ( !listening && segment.has( Segment::SYN ) && !segment.has( Segment::ACK ) && !segment.has( Segment::RST ) ) {
        passive_open( forward, port, segment, now, out );
      } else {
        reply_reset( forward, port, segment, out );
      }
      return;
    }

    switch ( c->state ) {
      case CONNECTING:
        if ( segment.has( Segment::RST ) ) {
          abort( *c, false, out );
        }
        /* otherwise it's the SYN again: they will hear from us when we know */
        break;

      case SYN_SENT:
        if ( segment.has( Segment::ACK ) && segment.ack != c->iss + 1 ) {
          reply_reset( forward, port, segment, out );
        } else if ( segment.has( Segment::RST ) ) {
          if ( segment.has( Segment::ACK ) ) {
            abort( *c, false, out ); /* connection refused */
          }
        } else if ( segment.has( Segment::SYN ) && segment.has( Segment::ACK ) ) {
          c->state = ESTABLISHED;
          c->snd_una = c->iss + 1;
          c->rcv_nxt = segment.seq + 1;
          c->snd_wnd = segment.window;
          c->mss = agreed_mss( segment.options );
          c->sack = offer_sack && SegmentOptions::sack_permitted( segment.options );
          if ( SegmentOptions::scale( segment.options ) >= 0 ) {
            c->snd_scale = SegmentOptions::scale( segment.options );
            c->rcv_scale = WINDOW_SHIFT;
          }
          c->cwnd = INITIAL_WINDOW_SEGMENTS * c->mss;
          c->rto_deadline = 0;
          c->retries = 0;
          emit( *c, Segment::ACK, c->snd_nxt, "", out );
        }
        break;

      case SYN_RCVD:
        if ( segment.has( Segment::RST ) ) {
          abort( *c, false, out );
        } else if ( segment.has( Segment::SYN ) ) {
          emit( *c, Segment::SYN | Segment::ACK, c->iss, "", out, true ); /* ours was lost */
        } else if ( segment.has( Segment::ACK ) && segment.ack == c->iss + 1 ) {
          c->state = ESTABLISHED;
          c->snd_una = c->iss + 1;
          c->snd_wnd = static_cast<uint32_t>( segment.window ) << c->snd_scale;
          c->cwnd = INITIAL_WINDOW_SEGMENTS * c->mss;
          c->rto_deadline = 0;
          c->retries = 0;
          established_input( *c, segment, now, out ); /* it may carry data already */
        }
        break;

      case ESTABLISHED:
        established_input( *c, segment, now, out );
        break;
    }

    sweep();
  }

  /*
   * How many bytes to read from this connection's socket now: 0 if none are wanted (it is not
   * open yet, has ended, or there is no room for more), otherwise at most that many.
   */
  size_t wants( Id id ) const
  {
    const Conn* c = find( id );
    if ( !c ) {
      return 0;
    }
    const size_t space = c->send_buf.size() < SEND_BUFFER ? SEND_BUFFER - c->send_buf.size() : 0;
    if ( c->dead || c->state != ESTABLISHED || c->local_eof || !space ) {
      return 0;
    }
    return std::min<size_t>( space, READ_AT_ONCE ); /* (also when the other end has gone: see forgotten()) */
  }

  /*
   * The application wrote these bytes (no more than wants() said); or, with length 0, it has
   * finished writing. They are sent on as the windows allow.
   */
  void read( Id id, const char* data, size_t length, uint64_t now, const Link& s_link, Out& out )
  {
    link = s_link;
    Conn* c = find( id );
    if ( c && wants( id ) ) {
      if ( c->peer_gone ) {
        if ( length == 0 ) {
          c->local_eof = true; /* (it has finished; there is nothing more to take from it) */
        }
        /* what it wrote is dropped: the other end has gone */
      } else if ( length > 0 ) {
        c->send_buf.append( data, length );
      } else {
        c->local_eof = true; /* a FIN will follow the data */
      }
      output( *c, now, out );
      maybe_finish( *c, out );
    }
    sweep();
  }

  /* Reading from this connection's socket failed: the application has gone. */
  void failed( Id id, const Link& s_link, Out& out )
  {
    link = s_link;
    Conn* c = find( id );
    if ( c ) {
      abort( *c, true, out );
    }
    sweep();
  }

  /* Timers, slow applications, and targets being reached. Call once each pass of the program's loop. */
  void tick( uint64_t now, const Link& s_link, Out& out )
  {
    link = s_link;
    /*
     * Back in contact after an absence: send again now whatever was waiting, rather than when a
     * timer backed off during the silence happens to expire.
     */
    const bool up = link.in_contact;
    if ( up && !link_was_up ) {
      for ( typename std::list<Conn>::iterator i = conns.begin(); i != conns.end(); i++ ) {
        if ( !i->dead && i->rto_deadline ) {
          i->retries = 0;
          i->rto = i->round_trip.settled();
          i->rto_deadline = now;
        }
      }
    }
    link_was_up = up;

    for ( typename std::list<Conn>::iterator i = conns.begin(); i != conns.end(); i++ ) {
      Conn& c = *i;
      if ( c.dead ) {
        continue;
      }

      if ( c.state == CONNECTING ) {
        const int reached = out.connecting( c.id() );
        if ( reached > 0 ) {
          connected( c, now, out );
        } else if ( reached < 0 || now >= c.connect_deadline ) {
          /* the target refused, or has not answered; so do we */
          c.state = SYN_RCVD;
          abort( c, true, out );
        }
        continue;
      }

      if ( !c.recv_buf.empty() ) {
        flush( c, out );
        maybe_finish( c, out );
      }
      if ( !c.dead && c.rto_deadline && now >= c.rto_deadline ) {
        timeout( c, now, out );
      }
    }
    sweep();
  }

  /* Milliseconds until tick() next has something to do; INT_MAX if nothing is pending. */
  int wait_time( uint64_t now ) const
  {
    int ret = INT_MAX;
    for ( typename std::list<Conn>::const_iterator i = conns.begin(); i != conns.end(); i++ ) {
      if ( i->dead ) {
        continue;
      }
      if ( i->state == CONNECTING || !i->recv_buf.empty() ) {
        ret = std::min( ret, POLL_INTERVAL );
      }
      if ( i->rto_deadline ) {
        ret = std::min<int64_t>( ret, i->rto_deadline > now ? i->rto_deadline - now : 0 );
      }
    }
    return ret;
  }

  /* The connections whose sockets are worth reading now (those for which wants() is not 0), in order. */
  std::vector<Id> readable( void ) const
  {
    std::vector<Id> ret;
    for ( typename std::list<Conn>::const_iterator i = conns.begin(); i != conns.end(); i++ ) {
      if ( !i->dead && i->state == ESTABLISHED && !i->local_eof && i->send_buf.size() < SEND_BUFFER ) {
        ret.push_back( i->id() );
      }
    }
    return ret;
  }

  /* How many connections there are, in any state. */
  size_t count( void ) const { return conns.size(); }
};

}

#endif
