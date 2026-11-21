/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * path-finding, the logic: where the other end might be, the probes sealed and numbered that find
 * out, which way is open, and the meetup registered with and passed through (REQ1/PATH-3,
 * REQ15/PATH-4, REQ18/PATH-5, REQ1/PATH-6; design v2, 2.2, path-finding's punch). Logic: the
 * time and datagrams (with where each came from) in; datagrams to send, and to where, out. No
 * socket, no clock, no randomness: punch.cc is its edge, and sends what it says.
 *
 * The fork's rules (mosh-1.4.0-pf.7, src/network/punch.cc, Punch), but at the places marked
 * (gosh): the meetup is tickets (src/meetup/), and a probe is opened only at its one length.
 *
 * On the socket there are four kinds of datagram, told apart by how they begin:
 *   probes   PUNCH_MARK, then a sealed message: "are you there", "I am", "goodbye". Sealed with
 *            a key the two ends share (sent over ssh), and numbered, so that only the other end
 *            can say where the other end is. Exactly PROBE_LEN bytes.
 *   STUN     answers from the servers asked; the meetup's says where to register from.
 *   meetup   REGISTERED and PASSED (src/meetup/messages.h), from the meetup's address only.
 *   the rest the session's own datagrams, taken only from where the other end was last heard
 *            to be. (They are mosh's, and sealed already.)
 *
 * Absent: no answer from anywhere: probes go on, every FAST_EVERY for FAST_FOR, then every
 * SLOW_EVERY. Late: a way not heard for OPEN_FOR is thought closed, and (gosh) so is one by which
 * our probes have not been answered for ANSWERED_FOR: a way must work both ways (REQ17/PATH-8:
 * found by review, 2026-11-16, IPv6 that went one way only). Wrong: what does not open,
 * a probe number already heard, a datagram from anywhere but the other end: ignored. Lying: a
 * STUN server's wrong answer is tried, and nothing answers there; a stranger cannot move the
 * way: only a sealed, fresh probe does.
 */

#ifndef GOSH_PATHFINDING_FINDING_H
#define GOSH_PATHFINDING_FINDING_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/crypto/crypto.h"
#include "src/meetup/messages.h"
#include "src/meetup/ticket.h"
#include "src/pathfinding/stun.h"

namespace Network {

/* The mark that begins a probe, and a control message between relays (relay's process.h). */
static const char PUNCH_MARK[8] = { 0, 'M', 'O', 'S', 'H', 'P', 'C', 'H' };

class PathFinding
{
public:
  static const uint64_t FAST_FOR = 8000;    /* ms of probing every... */
  static const uint64_t FAST_EVERY = 250;   /* ...this often, then every... */
  static const uint64_t SLOW_EVERY = 2000;  /* ...this often, while there is no way through */
  static const uint64_t OPEN_EVERY = 1000;  /* and this often once there is, to know that there still is */
  static const uint64_t OPEN_FOR = 3500;    /* how long without hearing before it is thought closed */
  static const uint64_t ANSWERED_FOR = 2 * OPEN_FOR; /* (gosh) and without an answer to our probes by it */
  static const size_t MAX_CANDIDATES = 16;
  static const size_t PROBE_LEN = 8 + 8 + 1 + 16; /* the mark; the nonce, one byte, the tag */
  static const uint64_t REGISTER_EVERY = 25000;   /* ms: registered again this often while it lasts... */
  static const uint64_t REGISTER_RETRY = 2000;    /* ...and this often until it is answered... */
  static const int REGISTER_TRIES = 3;            /* ...up to this many times; then where are we, again? */

  enum Got
  {
    NOTHING, /* nothing there to read (the edge's to say) */
    OTHER,   /* something of ours, or of nobody's: dealt with */
    DATA,    /* a datagram of the session's */
    GOODBYE  /* the other end has finished */
  };

  /* A datagram to send. */
  struct Outgoing
  {
    Addr to;
    std::string bytes;
  };

private:
  static constexpr uint64_t DIRECTION = uint64_t( 1 ) << 63; /* set in the numbers of probes from the end that did not make the key */

  bool initiator;
  std::unique_ptr<Crypto::Session> session;
  uint64_t send_seq, heard_seq;
  std::vector<Addr> candidates;       /* where the other end might be */
  std::vector<std::string> local_new; /* where we appear to be, not yet told to the other end */
  std::vector<std::string> local_all;
  Addr peer;     /* where it was last heard to be... */
  bool peer_via; /* ...or the meetup that passes datagrams on to it, if that is how it was heard */
  bool locked;
  uint64_t heard, started, last_probe, last_direct_probe;
  uint64_t answered;       /* (gosh) when a probe of ours was last answered by the way it is open by */
  uint64_t last_asked_back; /* (gosh) when we last asked back a new place a probe came from */
  std::vector<Outgoing> outbox;

  /* (gosh) the meetup, once there is a ticket for it: where it is, and where it saw us */
  bool meetup_known;
  Addr meetup;
  Gosh::Ticket ticket;
  int end; /* 0: the initiator; 1: the other */
  std::unique_ptr<Crypto::Session> meetup_session;
  uint64_t meetup_counter;
  std::string believed; /* where we appear to be, to the meetup ("where"), from its STUN answer */
  bool registered;
  int register_tries; /* REGISTERs not answered since we last asked where we are */
  uint64_t registered_at, last_register, last_meetup_stun;
  /* (gosh, REQ18/PATH-5) The meetup's clock, as we reckon it: the time of day when the meetup was
   * taken up, `now` then (taken at the first tick after), and what the meetup's TIME has told us
   * to add. And the counter of our last REGISTER, which a TIME must answer to be believed. */
  uint64_t meetup_wall, meetup_since;
  bool meetup_since_known;
  int64_t meetup_offset;
  uint64_t last_register_counter;
  bool time_wanted;

  uint64_t meetup_time( uint64_t now ) const
  {
    return static_cast<uint64_t>( static_cast<int64_t>( meetup_wall + ( now - meetup_since ) ) + meetup_offset );
  }

  void out( Addr to, const std::string& bytes )
  {
    Outgoing o;
    o.to = to;
    o.bytes = bytes;
    outbox.push_back( o );
  }

  /* Straight to an address; or to the meetup, to be passed on to the other end. */
  void transmit( const std::string& packet, Addr to, bool via )
  {
    if ( !via ) {
      out( to, packet );
    } else if ( registered ) {
      out( meetup, Gosh::meetup_seal( *meetup_session, Gosh::MEETUP_PASS, ticket.id, end, false, meetup_counter++, packet ) );
    }
  }

  void probe( char type, Addr to, bool via )
  {
    const uint64_t seq = ( ++send_seq ) | ( initiator ? 0 : DIRECTION );
    std::string packet( PUNCH_MARK, sizeof( PUNCH_MARK ) );
    packet += session->encrypt( Crypto::Message( Crypto::Nonce( seq ), std::string( 1, type ) ) );
    transmit( packet, to, via );
  }

  void add_local( const std::string& text, size_t most )
  {
    if ( text.empty() || local_all.size() >= most ) {
      return;
    }
    for ( size_t i = 0; i < local_all.size(); i++ ) {
      if ( local_all[i] == text ) {
        return;
      }
    }
    local_all.push_back( text );
    local_new.push_back( text );
  }

  /*
   * (gosh) Keeping our registration with the meetup: first where it sees us (its STUN answer),
   * then REGISTER with that, sealed; again until it answers, and every REGISTER_EVERY after.
   */
  void tend_meetup( uint64_t now )
  {
    if ( !meetup_known ) {
      return;
    }
    if ( !meetup_since_known ) {
      meetup_since = now;
      meetup_since_known = true;
    }
    if ( registered && now - registered_at > REGISTER_EVERY + OPEN_FOR ) {
      registered = false; /* not answered for a while: perhaps we have moved; ask again where we are */
      believed.clear();
    }
    if ( believed.empty() ) {
      if ( !last_meetup_stun || now - last_meetup_stun >= REGISTER_RETRY ) {
        last_meetup_stun = now;
        /* (any twelve bytes will do for its id: the answer is known by where it comes from) */
        out( meetup, stun_request( "gosh meetup?" ) );
      }
      return;
    }
    const uint64_t every = registered ? REGISTER_EVERY : REGISTER_RETRY;
    if ( !last_register || now - last_register >= every ) {
      if ( !registered && register_tries >= REGISTER_TRIES ) {
        /* Not taken from where we said: our NAT has moved us since. Ask again where we are. */
        believed.clear();
        register_tries = 0;
        last_meetup_stun = 0;
        return;
      }
      register_tries++;
      last_register = now;
      last_register_counter = meetup_counter;
      time_wanted = true;
      out( meetup,
           Gosh::meetup_seal( *meetup_session, Gosh::MEETUP_REGISTER, ticket.id, end, false, meetup_counter++,
                              Gosh::meetup_register_text( believed, meetup_time( now ) ) ) );
    }
  }

  /*
   * (gosh) A datagram from the meetup's address that is one of its messages: true, having dealt
   * with it. passed: the datagram inside, if it was a PASSED; empty otherwise.
   */
  bool from_meetup( const char* data, size_t n, uint64_t now, std::string& passed )
  {
    passed.clear();
    Gosh::MeetupHeader h;
    if ( !Gosh::meetup_read_header( data, n, h ) ) {
      return false;
    }
    if ( h.kind == Gosh::MEETUP_PASSED ) {
      passed.assign( h.sealed, h.sealed_len );
      return true;
    }
    uint64_t counter;
    std::string seen;
    if ( h.kind == Gosh::MEETUP_REGISTERED && h.ticket == ticket.id && h.end == end
         && Gosh::meetup_open( *meetup_session, h, true, counter, seen ) ) {
      /* (The meetup answers only from where we said we were; one that says otherwise is old, or lying.) */
      if ( seen == believed ) {
        registered = true;
        registered_at = now;
        register_tries = 0;
      }
    }
    std::optional<Gosh::Unsealed<Strong::Meetup::Time>> told;
    uint64_t answering, its_time;
    if ( h.kind == Gosh::MEETUP_TIME && h.ticket == ticket.id && h.end == end && time_wanted
         && Gosh::Sealed<Strong::Meetup::Time, Gosh::MeetupSealer>( h ).unseal( Gosh::MeetupSealer{ *meetup_session, true }, told )
         && Gosh::meetup_read_time( *told, answering, its_time ) && answering == last_register_counter ) {
      /* (gosh, REQ18/PATH-5) Our REGISTER's time was too far from the meetup's clock: take its
       * word, which answers our last REGISTER and so is not an old one, and register again now. */
      meetup_offset += static_cast<int64_t>( its_time - meetup_time( now ) );
      time_wanted = false;
      register_tries = 0;
      last_register = 0;
    }
    return true;
  }

public:
  /* key: as Crypto::Base64Key prints it; throws Crypto::CryptoException if it is not one. The end that made it up is the initiator. */
  PathFinding( bool s_initiator, const std::string& key, uint64_t now )
    : initiator( s_initiator ), session( new Crypto::Session( Crypto::Base64Key( key ) ) ), send_seq( 0 ), heard_seq( 0 ),
      candidates(), local_new(), local_all(), peer(), peer_via( false ), locked( false ), heard( 0 ), started( now ),
      last_probe( 0 ), last_direct_probe( 0 ), answered( 0 ), last_asked_back( 0 ), outbox(), meetup_known( false ), meetup(), ticket(),
      end( s_initiator ? 0 : 1 ), meetup_session(), meetup_counter( 0 ), believed(), registered( false ),
      register_tries( 0 ), registered_at( 0 ), last_register( 0 ), last_meetup_stun( 0 ), meetup_wall( 0 ), meetup_since( 0 ),
      meetup_since_known( false ), meetup_offset( 0 ), last_register_counter( 0 ), time_wanted( false )
  {}

  /* One of this machine's own addresses, with our port ("ip:port"): where the other end might reach us on our network. */
  void add_own_address( const std::string& ip_port ) { add_local( ip_port, MAX_CANDIDATES / 2 ); }

  /* Where the other end might be ("ip:port"), as it says. */
  void add_candidate( const std::string& ip_port )
  {
    Addr addr;
    if ( !parse_ip_port( ip_port, addr ) || candidates.size() >= MAX_CANDIDATES ) {
      return;
    }
    for ( size_t i = 0; i < candidates.size(); i++ ) {
      if ( same_ip_port( candidates[i], addr ) ) {
        return;
      }
    }
    candidates.push_back( addr );
    last_probe = 0; /* try it now */
  }

  /* Where we may be reached, as far as has been learnt since this was last asked. */
  std::vector<std::string> take_local_candidates( void )
  {
    std::vector<std::string> ret;
    ret.swap( local_new );
    return ret;
  }

  /*
   * (gosh) Use a meetup as well: at this address ("ip:port"), with this ticket. False if the
   * address or ticket is not one. wall_ms: the time of day, in ms; the counter its messages are
   * numbered by starts from it, so that a ticket used again by a new PathFinding is not taken
   * for an old one; and a REGISTER's time is reckoned from it until the meetup says otherwise.
   */
  bool use_meetup( const std::string& ip_port, const Gosh::Ticket& t, uint64_t wall_ms )
  {
    Addr where;
    if ( !parse_ip_port( ip_port, where ) || t.id.size() != Gosh::TICKET_ID_LEN || t.key.size() != Gosh::TICKET_KEY_LEN ) {
      return false;
    }
    meetup_session.reset( new Crypto::Session( Gosh::raw_key( t.key ) ) );
    meetup = where;
    meetup_known = true;
    ticket = t;
    meetup_counter = wall_ms << 10;
    meetup_wall = wall_ms;
    meetup_since_known = false;
    meetup_offset = 0;
    time_wanted = false;
    believed.clear();
    registered = false;
    register_tries = 0;
    last_register = last_meetup_stun = 0;
    return true;
  }
  bool meetup_registered( void ) const { return registered; }

  void tick( uint64_t now )
  {
    tend_meetup( now );
    if ( open( now ) ) {
      if ( now - last_probe >= OPEN_EVERY ) {
        last_probe = now;
        probe( '?', peer, peer_via );
      }
      /*
       * Through a meetup is second best: go on looking, now and then, for a way straight there.
       * (gosh, REQ17/PATH-8) And straight by IPv4 is second to straight by IPv6.
       */
      const bool by_ipv4 = !peer_via && peer.sa.sa_family == AF_INET;
      if ( ( peer_via || by_ipv4 ) && now - last_direct_probe >= SLOW_EVERY ) {
        last_direct_probe = now;
        for ( size_t i = 0; i < candidates.size(); i++ ) {
          if ( peer_via || candidates[i].sa.sa_family == AF_INET6 ) {
            probe( '?', candidates[i], false );
          }
        }
      }
      return;
    }
    /* No way through, that we know of.  Try everywhere the other end might be, and through the meetup. */
    const uint64_t every = ( now - started < FAST_FOR ) ? FAST_EVERY : SLOW_EVERY;
    if ( now - last_probe >= every ) {
      last_probe = now;
      for ( size_t i = 0; i < candidates.size(); i++ ) {
        probe( '?', candidates[i], false );
      }
      if ( registered ) {
        probe( '?', meetup, true );
      }
      if ( locked && !peer_via ) {
        probe( '?', peer, false );
      }
    }
  }

  /* A datagram has come from `from`. DATA: the session's, in datagram. Never NOTHING. */
  Got receive( uint64_t now, const char* whole, size_t n, const Addr& from, std::string& datagram )
  {
    if ( from.sa.sa_family != AF_INET && from.sa.sa_family != AF_INET6 ) {
      return OTHER;
    }

    /* Passed on by the meetup, from the other end?  Then what matters is what is inside. */
    const char* buf = whole;
    bool via = false;
    std::string passed;
    if ( meetup_known && same_ip_port( from, meetup ) && from_meetup( whole, n, now, passed ) ) {
      if ( passed.empty() ) {
        return OTHER;
      }
      via = true;
      buf = passed.data();
      n = passed.size();
    }

    if ( n > sizeof( PUNCH_MARK ) && memcmp( buf, PUNCH_MARK, sizeof( PUNCH_MARK ) ) == 0 ) {
      /* A probe.  Only the other end can have sealed it, and only once. (gosh) Opened only at its one length. */
      if ( n != PROBE_LEN ) {
        return OTHER;
      }
      try {
        const Crypto::Message m = session->decrypt( buf + sizeof( PUNCH_MARK ), n - sizeof( PUNCH_MARK ) );
        const uint64_t seq = m.nonce.val();
        const bool theirs = ( ( seq & DIRECTION ) != 0 ) == initiator;
        if ( !theirs || ( seq & ~DIRECTION ) <= heard_seq || m.text.size() != 1 ) {
          return OTHER;
        }
        heard_seq = seq & ~DIRECTION;
        /*
         * Wherever it is now, it is there: except that while there is a way
         * straight to it, hearing from it through a meetup as well is no
         * reason to go the long way round; (gosh, REQ17/PATH-8) and while
         * there is one by IPv6, hearing from it by IPv4 as well is no reason
         * to leave it.
         *
         * (gosh) And only an answer moves the way: a "?" shows only that its
         * datagrams reach us; a "!" answers one of ours, so what we send gets
         * there too. A "?" from a new place is answered, and asked back, so
         * that its answer can move the way.
         */
        const bool from_peer = locked && via == peer_via && same_ip_port( from, peer );
        const bool fresh = open( now );
        const bool longer_way = via && !peer_via;
        const bool older_family = !via && !peer_via && peer.sa.sa_family == AF_INET6 && from.sa.sa_family == AF_INET;
        const bool would_take = !( fresh && ( longer_way || older_family ) );
        if ( from_peer ) {
          heard = now;
          if ( m.text[0] == '!' ) {
            answered = now;
          }
        } else if ( m.text[0] == '!' && would_take ) {
          peer = from;
          peer_via = via;
          locked = true;
          heard = answered = now;
        }
        if ( m.text[0] == '?' ) {
          probe( '!', from, via ); /* (answered the way it came) */
          /* (not where it would not be taken, nor more often than FAST_EVERY: two ends asking each other back would never stop) */
          if ( !from_peer && would_take && now - last_asked_back >= FAST_EVERY ) {
            last_asked_back = now;
            probe( '?', from, via );
          }
        } else if ( m.text[0] == '.' ) {
          return GOODBYE;
        }
      } catch ( const Crypto::CryptoException& ) {
        /* not from the other end */
      }
      return OTHER;
    }
    if ( via ) {
      if ( locked && peer_via && same_ip_port( from, peer ) ) {
        heard = now;
        datagram.assign( buf, n );
        return DATA;
      }
      return OTHER;
    }

    Addr mapped;
    if ( stun_parse( buf, n, mapped ) ) {
      add_local( format_ip_port( mapped ), MAX_CANDIDATES );
      /* (gosh) The meetup's answer: where to register from. */
      if ( meetup_known && same_ip_port( from, meetup ) && believed.empty() ) {
        believed = Gosh::meetup_where_of( mapped );
        last_register = 0;
      }
      return OTHER;
    }

    if ( locked && !peer_via && same_ip_port( from, peer ) ) {
      heard = now;
      datagram.assign( buf, n );
      return DATA;
    }
    return OTHER;
  }

  /* The session's datagram, to the other end, if there is a way. */
  void send( const std::string& datagram )
  {
    if ( locked ) {
      transmit( datagram, peer, peer_via );
    }
  }

  /* Tell the other end we have finished. */
  void goodbye( void )
  {
    if ( locked ) {
      probe( '.', peer, peer_via );
    }
  }

  /* What to send, since this was last asked. */
  std::vector<Outgoing> take_outbox( void )
  {
    std::vector<Outgoing> ret;
    ret.swap( outbox );
    return ret;
  }

  /* Whether there is, as far as we know, a way through just now. */
  bool open( uint64_t now ) const { return locked && now - heard < OPEN_FOR && now - answered < ANSWERED_FOR; }
  uint64_t last_heard( void ) const { return locked ? heard : 0; }
  /* Where the other end is; or which meetup, if datagrams are being passed on to it through one. */
  std::string where( void ) const { return locked ? ( peer_via ? "the meetup at " : "" ) + format_ip_port( peer ) : std::string(); }
  bool direct( void ) const { return locked && !peer_via; }
};

}

#endif
