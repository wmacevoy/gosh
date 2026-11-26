/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-3, REQ15/PATH-4, REQ18/PATH-5: path-finding's logic (finding.h), with no socket and
 * no clock. Two ends and a meetup on a network made of a few lines here: addresses are numbers,
 * time moves when the test says, and "the NAT" is whether datagrams straight between the two ends
 * are let through, and where each end appears to be. The meetup is the meetup's own logic
 * (registrations.h, answer.h). The cipher is the real one.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "src/meetup/imp/answer.h"
#include "src/meetup/imp/registrations.h"
#include "src/pathfinding/finding.h"
#include "vendor/facts/facts.h"

using Network::Addr;
using Network::PathFinding;



namespace {
const std::string MASTER( "0123456789abcdef" );

Addr at( const std::string& ip_port )
{
  Addr a;
  Gosh::Stun::parse_ip_port( ip_port, a );
  return a;
}

/* The meetup's clock: UTC seconds, far from the ends' (whose time of day is given as 1 ms), so that each must learn it (REQ18/PATH-5). */
const uint64_t MEETUP_CLOCK = 1000000;

std::string key_of( const std::string& id )
{
  return Gosh::Meetup::ticket_key( MASTER, id );
}

/* The network: two ends, a meetup, and what lies between. */
struct Net
{
  PathFinding& a;
  PathFinding& b;
  Addr a_at, b_at;   /* where each appears to be, from outside */
  Addr a_at6, b_at6; /* (REQ17/PATH-8) and by IPv6, if it has that too: AF_UNSPEC if not */
  Addr meetup_at;
  bool straight;     /* whether datagrams straight between the two get through */
  bool a_to_b4_lost, a_to_b6_lost; /* (REQ17/PATH-8) one way only: what a sends straight to b's IPv4, or IPv6, address is lost */
  Gosh::Meetup::Imp::Registrations meetup;
  uint64_t now;      /* ms */
  std::string a_got, b_got;
  int b_goodbyes;
  int meetup_passed;
  int meetup_told_time; /* (REQ18/PATH-5) TIMEs the meetup answered: the ends' clocks here are 1970's */
  int straight_sent;

  Net( PathFinding& s_a, PathFinding& s_b )
    : a( s_a ), b( s_b ), a_at( at( "203.0.113.1:4000" ) ), b_at( at( "198.51.100.2:5000" ) ),
      a_at6(), b_at6(), meetup_at( at( "192.0.2.9:3478" ) ), straight( true ), a_to_b4_lost( false ), a_to_b6_lost( false ),
      meetup( MEETUP_CLOCK - 1000 ), now( 1000 ), a_got(), b_got(),
      b_goodbyes( 0 ), meetup_passed( 0 ), meetup_told_time( 0 ), straight_sent( 0 )
  {}

  void to_end( bool to_a, const std::string& bytes, Addr from )
  {
    std::string d;
    const PathFinding::Got got = ( to_a ? a : b ).receive( now, bytes.data(), bytes.size(), from, d );
    if ( got == PathFinding::DATA ) {
      ( to_a ? a_got : b_got ) = d;
    }
    if ( got == PathFinding::GOODBYE && !to_a ) {
      b_goodbyes++;
    }
  }

  /* Everything the two ends want to send goes, until there is nothing more. */
  void deliver( void )
  {
    for ( int round = 0; round < 20; round++ ) {
      bool any = false;
      for ( int side = 0; side < 2; side++ ) {
        PathFinding& from = side ? b : a;
        const std::vector<PathFinding::Outgoing> out = from.take_outbox();
        for ( size_t i = 0; i < out.size(); i++ ) {
          any = true;
          const Addr& to = out[i].to;
          const std::string& bytes = out[i].bytes;
          /* (it is seen from whichever of its addresses is of the family it sends to) */
          const Addr& from_at = to.sa.sa_family == AF_INET6 ? ( side ? b_at6 : a_at6 ) : ( side ? b_at : a_at );
          if ( from_at.sa.sa_family != to.sa.sa_family ) {
            continue; /* it has no address of that family: nothing goes */
          }
          if ( Gosh::Stun::same_ip_port( to, meetup_at ) ) {
            if ( Gosh::Stun::is_stun_request( bytes.data(), bytes.size() ) ) {
              const std::string answer = Gosh::Meetup::Imp::stun_answer_to( bytes.data(), Gosh::Meetup::Imp::meetup_where_of( from_at ) );
              to_end( side == 0, answer, meetup_at );
              continue;
            }
            const Gosh::Meetup::Imp::Registrations::Send s
              = meetup.datagram( bytes.data(), bytes.size(), Gosh::Meetup::Imp::meetup_where_of( from_at ), MEETUP_CLOCK + now / 1000, key_of );
            Addr dest;
            socklen_t len;
            Gosh::Meetup::Imp::MeetupHeader h;
            if ( s.any && Gosh::Meetup::Imp::meetup_addr_of( s.to, dest, len ) && Gosh::Meetup::Imp::meetup_read_header( s.bytes.data(), s.bytes.size(), h ) ) {
              meetup_passed += h.kind == Gosh::Meetup::Imp::MEETUP_PASSED;
              meetup_told_time += h.kind == Gosh::Meetup::Imp::MEETUP_TIME;
              if ( is( dest, true ) ) {
                to_end( true, s.bytes, meetup_at );
              } else if ( is( dest, false ) ) {
                to_end( false, s.bytes, meetup_at );
              }
            }
            continue;
          }
          const bool to_other = is( to, side != 0 );
          if ( side == 0 && ( ( a_to_b4_lost && Gosh::Stun::same_ip_port( to, b_at ) ) || ( a_to_b6_lost && Gosh::Stun::same_ip_port( to, b_at6 ) ) ) ) {
            continue;
          }
          if ( to_other ) {
            straight_sent++;
            if ( straight ) {
              to_end( side != 0, bytes, from_at );
            }
          }
        }
      }
      if ( !any ) {
        return;
      }
    }
  }

  /* Whether an address is one of end 0's (a) or end 1's. */
  bool is( const Addr& where, bool end_a ) const
  {
    return Gosh::Stun::same_ip_port( where, end_a ? a_at : b_at ) || Gosh::Stun::same_ip_port( where, end_a ? a_at6 : b_at6 );
  }

  /* Time passes, in steps of 50 ms, with both ends ticking and everything delivered. */
  void pass( uint64_t ms )
  {
    for ( uint64_t t = 0; t < ms; t += 50 ) {
      now += 50;
      a.tick( now );
      b.tick( now );
      deliver();
    }
  }
};
}
/* REQ1/PATH-3, REQ15/PATH-4, REQ18/PATH-5, REQ17/PATH-8: path-finding's logic */
FACTS( Finding )
{
  const std::string key = Crypto::Base64Key().printable_key();
  const Gosh::Meetup::Ticket ticket = Gosh::Meetup::make_ticket( MASTER, "random!!", 2000000000 );

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    net.pass( 1000 );
    FACT_CHECK( ( a.open( net.now ) && b.open( net.now ) && a.direct() && b.direct() ), "each told where the other is: a way straight through, both ways (REQ1/PATH-3)" );
    FACT_CHECK( ( a.where() == "198.51.100.2:5000" && b.where() == "203.0.113.1:4000" ), "each knows where the other is" );
    a.send( "a datagram of the session's" );
    net.deliver();
    FACT_CHECK( net.b_got == "a datagram of the session's", "a datagram sent goes straight to the other end" );

    net.straight = false;
    net.pass( PathFinding::OPEN_FOR + 1000 );
    FACT_CHECK( ( !a.open( net.now ) && !b.open( net.now ) ), "when nothing comes back for 3.5 s, the way is thought closed" );
    net.straight = true;
    net.pass( 2500 );
    FACT_CHECK( ( a.open( net.now ) && b.open( net.now ) ), "and found again when it opens" );

    a.goodbye();
    net.deliver();
    FACT_CHECK( net.b_goodbyes == 1, "a goodbye reaches the other end" );
  }

  /* REQ18/PATH-5: a TIME is believed only as the answer to our last REGISTER, so an old one sent again moves no clock. */
  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.straight = false;
    a.use_meetup( "192.0.2.9:3478", ticket, 1 );
    b.use_meetup( "192.0.2.9:3478", ticket, 1 );
    net.pass( 1000 );
    Crypto::Session as_meetup( Gosh::Meetup::raw_key( ticket.key ) );
    const uint64_t wrong = ( MEETUP_CLOCK + 86400 ) * 1000; /* a day ahead */
    net.to_end( true, Gosh::Meetup::Imp::meetup_seal( as_meetup, Gosh::Meetup::Imp::MEETUP_TIME, ticket.id, 0, true, 99999, Gosh::Meetup::Imp::meetup_time_text( 7, wrong ) ),
                net.meetup_at );
    std::string reg;
    for ( int i = 0; i < 1000 && reg.empty(); i++ ) {
      net.now += 50;
      a.tick( net.now );
      const std::vector<PathFinding::Outgoing> out = a.take_outbox();
      for ( size_t j = 0; j < out.size(); j++ ) {
        Gosh::Meetup::Imp::MeetupHeader h;
        if ( Gosh::Meetup::Imp::meetup_read_header( out[j].bytes.data(), out[j].bytes.size(), h ) && h.kind == Gosh::Meetup::Imp::MEETUP_REGISTER ) {
          reg = out[j].bytes;
        }
      }
    }
    Gosh::Meetup::Imp::MeetupHeader h;
    uint64_t said = 0;
    std::optional<Gosh::Meetup::Imp::Unsealed<Strong::Meetup::Register>> text;
    const bool opened = Gosh::Meetup::Imp::meetup_read_header( reg.data(), reg.size(), h )
                        && Gosh::Meetup::Imp::Sealed<Strong::Meetup::Register, Gosh::Meetup::Imp::MeetupSealer>( h ).unseal( Gosh::Meetup::Imp::MeetupSealer{ as_meetup, false }, text )
                        && Gosh::Meetup::Imp::meetup_read_register( *text, said );
    const uint64_t meetup_ms = ( MEETUP_CLOCK + net.now / 1000 ) * 1000;
    FACT_CHECK( ( a.meetup_registered() && opened && said + 5000 > meetup_ms && said < meetup_ms + 5000 ), "a TIME that answers no REGISTER of ours is not believed: the next REGISTER keeps the meetup's clock" );
  }

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.straight = false;
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    FACT_CHECK( ( a.use_meetup( "192.0.2.9:3478", ticket, 1 )
                                                                 && b.use_meetup( "192.0.2.9:3478", ticket, 1 ) ), "a meetup is taken at an address, with a ticket" );
    FACT_CHECK( !a.use_meetup( "meetup.example:1", ticket, 1 ), "and not at what is not one" );
    a.use_meetup( "192.0.2.9:3478", ticket, 1 );
    net.pass( 1000 );
    FACT_CHECK( ( a.meetup_registered() && b.meetup_registered() ), "nothing straight gets through, and both register with the meetup (REQ18/PATH-5)" );
    FACT_CHECK( net.meetup_told_time == 2, "each by the meetup's clock, which it was told (TIME), its own being far off (REQ18/PATH-5)" );
    net.pass( 1000 );
    FACT_CHECK( ( a.open( net.now ) && b.open( net.now ) && !a.direct() && !b.direct()
             && a.where() == "the meetup at 192.0.2.9:3478" ), "and find each other through it: open, and not straight (REQ15/PATH-4)" );
    const int before = net.meetup_passed;
    a.send( "through the meetup" );
    net.deliver();
    FACT_CHECK( ( net.b_got == "through the meetup" && net.meetup_passed > before ), "a datagram sent goes through the meetup to the other end" );

    net.straight = true;
    net.pass( 2 * PathFinding::SLOW_EVERY + 500 );
    FACT_CHECK( ( a.direct() && b.direct() ), "a way straight through is still looked for, and taken when it opens" );
    net.b_got.clear();
    const int via = net.meetup_passed;
    a.send( "straight again" );
    net.deliver();
    FACT_CHECK( ( net.b_got == "straight again" && net.meetup_passed == via ), "and then datagrams go straight, not through the meetup" );
    std::string probe;
    for ( int i = 0; i < 40 && probe.empty(); i++ ) {
      net.now += 50;
      b.tick( net.now );
      const std::vector<PathFinding::Outgoing> out = b.take_outbox();
      for ( size_t j = 0; j < out.size(); j++ ) {
        if ( out[j].bytes.size() == PathFinding::PROBE_LEN ) {
          probe = out[j].bytes;
        }
      }
    }
    net.to_end( true, Gosh::Meetup::Imp::meetup_passed( probe ), net.meetup_at );
    FACT_CHECK( ( !probe.empty() && a.direct() ), "with a way straight through, a probe heard through the meetup as well does not turn it back" );

    net.straight = false;
    net.a_at = at( "203.0.113.1:4444" ); /* the NAT moves end 0 to another port */
    net.pass( Gosh::Meetup::End::REGISTER_EVERY + PathFinding::OPEN_FOR + 6000 );
    FACT_CHECK( ( a.meetup_registered() && a.open( net.now ) && b.open( net.now ) && !a.direct() ), "end 0 moved by its NAT: it registers again from where it now is, and the way through holds" );
  }

  {
    /* The NAT moves end 0 between its asking the meetup where it is and its registering. */
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.straight = false;
    a.use_meetup( "192.0.2.9:3478", ticket, 1 );
    a.tick( net.now );
    net.deliver(); /* the STUN question and answer: end 0 believes it is at :4000 */
    Crypto::Session liar( Gosh::Meetup::raw_key( ticket.key ) );
    net.to_end( true, Gosh::Meetup::Imp::meetup_seal( liar, Gosh::Meetup::Imp::MEETUP_REGISTERED, ticket.id, 0, true, 7,
                                         Gosh::Meetup::Imp::meetup_where_v4( 0xCB007101, 9999 ) ), net.meetup_at );
    FACT_CHECK( !a.meetup_registered(), "a REGISTERED that says it saw us somewhere we did not say is not taken" );
    net.a_at = at( "203.0.113.1:4001" );
    net.pass( 2 * Gosh::Meetup::End::REGISTER_RETRY );
    FACT_CHECK( !a.meetup_registered(), "moved before registering: its REGISTERs are refused" );
    net.pass( 3 * Gosh::Meetup::End::REGISTER_RETRY + 3000 );
    FACT_CHECK( a.meetup_registered(), "so it asks again where it is, and registers from there" );
  }

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    const Gosh::Meetup::Ticket forged = Gosh::Meetup::make_ticket( "not the master!!", "random!!", 2000000000 );
    net.straight = false;
    a.use_meetup( "192.0.2.9:3478", forged, 1 );
    b.use_meetup( "192.0.2.9:3478", forged, 1 );
    net.pass( 5000 );
    FACT_CHECK( ( !a.meetup_registered() && !b.meetup_registered() && !a.open( net.now ) ), "with a ticket the meetup did not make, neither registers, and no way is found" );
  }

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.straight = false;
    a.add_candidate( "198.51.100.2:5000" );
    net.pass( PathFinding::FAST_FOR );
    const int fast = net.straight_sent;
    net.straight_sent = 0;
    net.pass( 8000 );
    const int slow = net.straight_sent;
    FACT_CHECK( ( fast >= 30 && fast <= 34 ), "with no way through, it probes every 250 ms for 8 s (about 32)" );
    FACT_CHECK( ( slow >= 3 && slow <= 5 ), "and then every 2 s (about 4 in 8 s)" );
  }

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    net.pass( 1000 );

    std::string d;
    std::string huge( Network::PUNCH_MARK, sizeof( Network::PUNCH_MARK ) );
    huge += std::string( 4000, 'x' );
    FACT_CHECK( b.receive( net.now, huge.data(), huge.size(), net.a_at, d ) == PathFinding::OTHER, "a probe-marked datagram of 4,008 bytes is not opened (the fifth security fault)" );
    std::string short_probe( Network::PUNCH_MARK, sizeof( Network::PUNCH_MARK ) );
    short_probe += "abc";
    FACT_CHECK( b.receive( net.now, short_probe.data(), short_probe.size(), net.a_at, d ) == PathFinding::OTHER, "nor one too short" );

    /* A probe of end 0's, copied as it went, and sent later from somewhere else. */
    a.goodbye(); /* (any probe will do; this one is easy to find) */
    const std::vector<PathFinding::Outgoing> sent = a.take_outbox();
    const std::string copied = sent.empty() ? std::string() : sent[0].bytes;
    net.to_end( false, copied, net.a_at );
    const std::string before = b.where();
    const Addr onlooker = at( "192.0.2.66:6666" );
    net.to_end( false, copied, onlooker );
    FACT_CHECK( b.where() == before, "a probe sent again, from elsewhere, does not move the way" );
    std::string sealed_elsewhere( Network::PUNCH_MARK, sizeof( Network::PUNCH_MARK ) );
    sealed_elsewhere += std::string( PathFinding::PROBE_LEN - sizeof( Network::PUNCH_MARK ), 'z' );
    net.to_end( false, sealed_elsewhere, onlooker );
    FACT_CHECK( b.where() == before, "nor does one not sealed with the key" );
    FACT_CHECK( b.receive( net.now, "hello", 5, onlooker, d ) == PathFinding::OTHER, "and a datagram from anywhere but the other end is not the session's" );

    const std::string answer = Gosh::Meetup::Imp::stun_answer( std::string( "\x00\x01\x00\x00\x21\x12\xa4\x42" "twelve bytes", 20 ).data(),
                                                  0xCB007101, 7777 );
    b.take_local_candidates();
    b.receive( net.now, answer.data(), answer.size(), at( "192.0.2.50:3478" ), d );
    const std::vector<std::string> mine = b.take_local_candidates();
    FACT_CHECK( ( mine.size() == 1 && mine[0] == "203.0.113.1:7777" ), "a STUN server's answer gives a new place where we may be reached" );
  }

  /* REQ17/PATH-8: by IPv6. */
  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    memset( &net.a_at, 0, sizeof( net.a_at ) ); /* (no IPv4 address at all) */
    memset( &net.b_at, 0, sizeof( net.b_at ) );
    net.a_at6 = at( "[2001:db8::a]:4000" );
    net.b_at6 = at( "[2001:db8::b]:5000" );
    a.add_candidate( "[2001:db8::b]:5000" );
    b.add_candidate( "[2001:db8::a]:4000" );
    net.pass( 1000 );
    FACT_CHECK( ( a.open( net.now ) && a.direct() && a.where() == "[2001:db8::b]:5000" ), "two ends with IPv6 alone find a way straight through by it (REQ17/PATH-8)" );
  }
  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    memset( &net.a_at, 0, sizeof( net.a_at ) ); /* (no IPv4 address at all) */
    memset( &net.b_at, 0, sizeof( net.b_at ) );
    net.a_at6 = at( "[2001:db8::a]:4000" );
    net.b_at6 = at( "[2001:db8::b]:5000" );
    net.meetup_at = at( "[2001:db8::9]:3478" );
    net.straight = false;
    a.use_meetup( "[2001:db8::9]:3478", ticket, 1 );
    b.use_meetup( "[2001:db8::9]:3478", ticket, 1 );
    net.pass( 2000 );
    FACT_CHECK( ( a.meetup_registered() && b.meetup_registered() && a.open( net.now ) && !a.direct()
             && a.where() == "the meetup at [2001:db8::9]:3478" ), "and, nothing straight getting through, through a meetup with an IPv6 address alone" );
    a.send( "by IPv6, through the meetup" );
    net.deliver();
    FACT_CHECK( net.b_got == "by IPv6, through the meetup", "and a datagram goes through it" );
  }
  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.a_at6 = at( "[2001:db8::a]:4000" );
    net.b_at6 = at( "[2001:db8::b]:5000" );
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    net.pass( 1000 );
    FACT_CHECK( ( a.direct() && a.where() == "198.51.100.2:5000" ), "two ends with both families, told only each other's IPv4: by IPv4" );
    a.add_candidate( "[2001:db8::b]:5000" );
    b.add_candidate( "[2001:db8::a]:4000" );
    net.pass( 3 * PathFinding::OPEN_EVERY );
    FACT_CHECK( ( a.where() == "[2001:db8::b]:5000" && b.where() == "[2001:db8::a]:4000" ), "told their IPv6 as well, both move to it, and stay (REQ17/PATH-8: IPv6 first)" );
    net.pass( 5 * PathFinding::OPEN_EVERY );
    FACT_CHECK( a.where() == "[2001:db8::b]:5000", "and IPv4's answers go on without taking them back to it" );
    std::string probe;
    for ( int i = 0; i < 40 && probe.empty(); i++ ) {
      net.now += 50;
      b.tick( net.now );
      const std::vector<PathFinding::Outgoing> out = b.take_outbox();
      for ( size_t j = 0; j < out.size(); j++ ) {
        if ( out[j].bytes.size() == PathFinding::PROBE_LEN ) {
          probe = out[j].bytes;
        }
      }
    }
    a.take_outbox();
    net.to_end( true, probe, net.b_at );
    FACT_CHECK( ( !probe.empty() && a.where() == "[2001:db8::b]:5000" ), "a fresh probe from the other end by IPv4 does not take it off IPv6 while that answers" );
    FACT_CHECK( a.take_outbox().size() == 1, "and it is answered, and not asked back (two ends asking each other back would never stop)" );
  }

  /* REQ17/PATH-8: IPv6 that works one way only (found by review, 2026-11-16: both ends said the
     way was open and nothing went either way, for good) */
  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.a_at6 = at( "[2001:db8::a]:4000" );
    net.b_at6 = at( "[2001:db8::b]:5000" );
    net.a_to_b6_lost = true;
    a.add_candidate( "198.51.100.2:5000" );
    a.add_candidate( "[2001:db8::b]:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    b.add_candidate( "[2001:db8::a]:4000" );
    net.pass( 20000 );
    FACT_CHECK( ( a.open( net.now ) && b.open( net.now ) && a.where() == "198.51.100.2:5000" && b.where() == "203.0.113.1:4000" ), "IPv6 one way only: both ends take IPv4, which works both ways" );
    net.a_got.clear();
    net.b_got.clear();
    a.send( "from a" );
    b.send( "from b" );
    net.deliver();
    FACT_CHECK( ( net.b_got == "from a" && net.a_got == "from b" ), "and datagrams arrive both ways" );
  }
  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    net.pass( 1000 );
    const bool opened = a.open( net.now ) && b.open( net.now );
    net.a_to_b4_lost = true; /* from now, what a sends is lost; what b sends still arrives */
    net.pass( 2 * PathFinding::OPEN_FOR + 4 * PathFinding::OPEN_EVERY );
    FACT_CHECK( ( opened && !a.open( net.now ) ), "a way that goes one way only, once open, is thought closed at the end it cannot send from" );
  }
  {
    /* An end moved to where the other was never told it might be: its probes come from there, and
       only an answer moves the way, so the other end asks back (REQ1/PATH-3: "when either end's
       address changes it finds another"). */
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    net.pass( 1000 );
    net.a_at = at( "203.0.113.77:4444" );
    net.pass( 2 * PathFinding::OPEN_FOR );
    FACT_CHECK( ( b.open( net.now ) && b.where() == "203.0.113.77:4444" && a.open( net.now ) ), "an end moved to a place the other was not told of is found there, both ways" );
  }
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Finding );
}

FACTS_MAIN
