/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING). gosh carries mosh's code
    and is not the mosh project's work.

    The algorithm here is that of AdapterPort in wmacevoy/mosh at
    mosh-1.4.0-pf.7 (src/network/tcpadapter.cc), statement for statement, with
    its sockets, its clock and its random numbers taken out and passed in.
*/

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

#include "src/forwarding/tcpinudp.h"

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
 * both ways including zero-window probes, window scaling, half-close of each direction, resets.
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

/*
 * Bytes waiting in order: added at the end, taken from the front.
 *
 * A connection's two buffers are these. Taking from the front of a std::string moves everything
 * left in it down, and the sender did that with up to 1 MB on every acknowledgment: about 40% of
 * all the processor time of a forward, by a profile of the fork (docs/FINDINGS.md, 2026-11-14).
 * Here the front is only marked as gone, and what is left is moved down once the gone part is
 * at least 64 KB and at least as large as what is left: so no byte is moved more than once for
 * each byte taken, and no more than twice the bytes held are ever kept.
 */
class Bytes
{
  std::string bytes;
  size_t gone; /* how much at the front of `bytes` has been taken */

public:
  static const size_t TIDY_AT = 65536;

  Bytes() : bytes(), gone( 0 ) {}

  size_t size( void ) const { return bytes.size() - gone; }
  bool empty( void ) const { return size() == 0; }
  const char* data( void ) const { return bytes.data() + gone; }

  void append( const char* more, size_t length ) { bytes.append( more, length ); }
  /* length bytes of `from`, starting at `at`. */
  void append( const std::string& from, size_t at, size_t length ) { bytes.append( from, at, length ); }

  /* Up to length bytes starting `at` bytes in, as a string. */
  std::string substr( size_t at, size_t length ) const { return bytes.substr( gone + at, length ); }

  /* Takes this many from the front (all of them, if there are fewer). */
  void drop( size_t length )
  {
    gone += std::min( length, size() );
    if ( gone == bytes.size() ) {
      bytes.clear();
      gone = 0;
    } else if ( gone >= TIDY_AT && gone >= bytes.size() - gone ) {
      bytes.erase( 0, gone );
      gone = 0;
    }
  }
};

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

  static constexpr uint32_t RTO_INITIAL = 1000; /* ms */
  static constexpr uint32_t RTO_MIN = 200;
  static constexpr uint32_t RTO_MAX = 30000;
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
    bool rtt_valid;
    double srtt, rttvar;

    /* receiving: bytes from the other end, on their way to the socket */
    Bytes recv_buf;
    uint32_t rcv_nxt;
    std::map<uint32_t, std::string> early; /* segments from beyond rcv_nxt, by sequence number */
    size_t early_bytes;
    bool fin_received;
    bool shut_wr;         /* we have passed their FIN on to the socket */
    uint32_t last_window; /* what we last said we would accept, in bytes */

    uint64_t connect_deadline;

    Conn()
      : forward( 0 ), port( 0 ), state( CONNECTING ), dead( false ), mss( DEFAULT_MSS ), send_buf(), iss( 0 ),
        snd_una( 0 ), snd_nxt( 0 ), snd_max( 0 ), snd_wnd( 0 ), snd_scale( 0 ), rcv_scale( 0 ), cwnd( 0 ),
        ssthresh( 0xFFFFFFFF ), dupacks( 0 ), recovering( false ), recover( 0 ), holes( 0 ), resending( false ),
        calm_until( 0 ), local_eof( false ), fin_sent( false ), fin_was_sent( false ), fin_acked( false ), fin_seq( 0 ),
        rto_deadline( 0 ), rto( RTO_INITIAL ), retries( 0 ), rtt_timing( false ), rtt_seq( 0 ), rtt_start( 0 ),
        rtt_valid( false ), srtt( 0 ), rttvar( 0 ), recv_buf(), rcv_nxt( 0 ), early(), early_bytes( 0 ),
        fin_received( false ), shut_wr( false ), last_window( 0 ), connect_deadline( 0 )
    {}

    Id id( void ) const { return id_of( forward, port ); }
  };

  bool listening; /* this end listens for applications (the client, for -L); the other connects to targets */
  std::list<Conn> conns;
  uint16_t next_port;
  bool link_was_up;
  Link link; /* as given to the call now being made */

  /* sequence numbers wrap */
  static bool seq_lt( uint32_t a, uint32_t b ) { return static_cast<int32_t>( a - b ) < 0; }

  static uint32_t mss_option( const std::string& options )
  {
    for ( size_t i = 0; i < options.size(); ) {
      const uint8_t kind = options[i];
      if ( kind == 0 ) {
        break;
      }
      if ( kind == 1 ) {
        i++;
        continue;
      }
      if ( i + 1 >= options.size() ) {
        break;
      }
      const uint8_t len = options[i + 1];
      if ( len < 2 || i + len > options.size() ) {
        break;
      }
      if ( kind == 2 && len == 4 ) {
        return ( static_cast<uint8_t>( options[i + 2] ) << 8 ) | static_cast<uint8_t>( options[i + 3] );
      }
      i += len;
    }
    return DEFAULT_MSS;
  }

  /* The window scale option: the shift asked for, or -1 if it was not. */
  static int scale_option( const std::string& options )
  {
    for ( size_t i = 0; i < options.size(); ) {
      const uint8_t kind = options[i];
      if ( kind == 0 ) {
        break;
      }
      if ( kind == 1 ) {
        i++;
        continue;
      }
      if ( i + 1 >= options.size() ) {
        break;
      }
      const uint8_t len = options[i + 1];
      if ( len < 2 || i + len > options.size() ) {
        break;
      }
      if ( kind == 3 && len == 3 ) {
        return std::min<int>( static_cast<uint8_t>( options[i + 2] ), 14 );
      }
      i += len;
    }
    return -1;
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
    return std::max<uint32_t>( MIN_MSS, std::min( mss_option( their_options ), local_mss() ) );
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
    if ( tell_peer && c.state != CONNECTING ) {
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
    if ( scale_option( syn.options ) >= 0 ) {
      c->snd_scale = scale_option( syn.options );
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
        abort( c, false, out );
      }
      return;
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
      const uint32_t ahead = seq - c.rcv_nxt;
      if ( !payload.empty() && ahead <= RECV_BUFFER && c.early_bytes + payload.size() <= RECV_BUFFER
           && c.early.insert( std::make_pair( seq, payload ) ).second ) {
        c.early_bytes += payload.size();
      }
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
      c.early_bytes = 0;
      return;
    }

    /* Does something we were holding come next now? */
    for ( std::map<uint32_t, std::string>::iterator i = c.early.begin(); i != c.early.end(); ) {
      const uint32_t behind = c.rcv_nxt - i->first;
      if ( behind > RECV_BUFFER ) {
        i++; /* still ahead */
        continue;
      }
      const std::string held = i->second;
      const uint32_t held_seq = i->first;
      c.early_bytes -= held.size();
      c.early.erase( i );
      if ( behind <= held.size() ) {
        accept_data( c, held_seq, held, false );
        return; /* which has looked through the rest */
      }
      i = c.early.begin(); /* that one was stale; start over */
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
          retransmit_one( c, out );
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
    c.snd_wnd = their_window;
    c.dupacks = 0;
    c.retries = 0;

    /* round-trip time, from a segment that was sent only once */
    if ( c.rtt_timing && !seq_lt( ack, c.rtt_seq ) ) {
      const double r = now - c.rtt_start;
      if ( !c.rtt_valid ) {
        c.srtt = r;
        c.rttvar = r / 2;
        c.rtt_valid = true;
      } else {
        c.rttvar = 0.75 * c.rttvar + 0.25 * fabs( c.srtt - r );
        c.srtt = 0.875 * c.srtt + 0.125 * r;
      }
      c.rtt_timing = false;
    }
    c.rto = settled_rto( c );

    if ( c.recovering ) {
      if ( seq_lt( ack, c.recover ) ) {
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
    if ( c.dead || c.state != ESTABLISHED ) {
      return;
    }

    /* What fits in a datagram can shrink while a connection is open, if mosh has to lower its own MTU. */
    const uint32_t mss = std::min( c.mss, local_mss() );

    while ( true ) {
      const uint32_t flight = c.snd_nxt - c.snd_una;
      const uint32_t data_sent = flight - ( c.fin_sent && !c.fin_acked ? 1 : 0 );
      const size_t unsent = c.send_buf.size() - data_sent;

      uint32_t allowed = std::min( c.snd_wnd, c.cwnd );
      if ( allowed == 0 && flight == 0 ) {
        allowed = 1; /* probe a closed window, or we could wait forever for it to open */
      }

      const bool fresh = !seq_lt( c.snd_nxt, c.snd_max );

      if ( unsent ) {
        if ( flight >= allowed ) {
          break;
        }
        const size_t n = std::min<size_t>( std::min<size_t>( unsent, mss ), allowed - flight );
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

  /* The retransmission timeout when nothing has gone wrong: from the measured round trip, if there is one. */
  uint32_t settled_rto( const Conn& c ) const
  {
    if ( !c.rtt_valid ) {
      return RTO_INITIAL;
    }
    const double rto = c.srtt + std::max( 4 * c.rttvar, 10.0 );
    return std::min<double>( std::max<double>( rto, RTO_MIN ), RTO_MAX );
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
    if ( c.state == ESTABLISHED && c.last_window < c.mss && window( c ) >= 2 * c.mss ) {
      emit( c, Segment::ACK, c.snd_nxt, "", out );
    }
  }

  /* unused */
  Connections( const Connections& );
  Connections& operator=( const Connections& );

public:
  /* s_listening: this end listens for applications; the other end connects to targets. */
  explicit Connections( bool s_listening )
    : listening( s_listening ), conns(), next_port( 32768 ), link_was_up( true ), link()
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
          if ( scale_option( segment.options ) >= 0 ) {
            c->snd_scale = scale_option( segment.options );
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
    return std::min<size_t>( space, READ_AT_ONCE );
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
      if ( length > 0 ) {
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
          i->rto = settled_rto( *i );
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
