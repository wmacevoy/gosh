/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-3, REQ15/PATH-4, REQ15/PATH-5: path-finding's logic (finding.h), with no socket and
 * no clock. Two ends and a meetup on a network made of a few lines here: addresses are numbers,
 * time moves when the test says, and "the NAT" is whether datagrams straight between the two ends
 * are let through, and where each end appears to be. The meetup is the meetup's own logic
 * (registrations.h, answer.h). The cipher is the real one.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "src/meetup/answer.h"
#include "src/meetup/registrations.h"
#include "src/pathfinding/finding.h"

using Network::Addr;
using Network::PathFinding;

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

namespace {
const std::string MASTER( "0123456789abcdef" );

Addr at( const std::string& ip_port )
{
  Addr a;
  Network::parse_ip_port( ip_port, a );
  return a;
}

std::string key_of( const std::string& id )
{
  return Gosh::ticket_key( MASTER, id );
}

/* The network: two ends, a meetup, and what lies between. */
struct Net
{
  PathFinding& a;
  PathFinding& b;
  Addr a_at, b_at;   /* where each appears to be, from outside */
  Addr a_at6, b_at6; /* (REQ16/PATH-8) and by IPv6, if it has that too: AF_UNSPEC if not */
  Addr meetup_at;
  bool straight;     /* whether datagrams straight between the two get through */
  Gosh::Registrations meetup;
  uint64_t now;      /* ms */
  std::string a_got, b_got;
  int b_goodbyes;
  int meetup_passed;
  int straight_sent;

  Net( PathFinding& s_a, PathFinding& s_b )
    : a( s_a ), b( s_b ), a_at( at( "203.0.113.1:4000" ) ), b_at( at( "198.51.100.2:5000" ) ),
      a_at6(), b_at6(), meetup_at( at( "192.0.2.9:3478" ) ), straight( true ), meetup(), now( 1000 ), a_got(), b_got(),
      b_goodbyes( 0 ), meetup_passed( 0 ), straight_sent( 0 )
  {}

  void to_end( bool to_a, const std::string& bytes, const Addr& from )
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
          if ( Network::same_ip_port( to, meetup_at ) ) {
            if ( Gosh::is_stun_request( bytes.data(), bytes.size() ) ) {
              const std::string answer = Gosh::stun_answer_to( bytes.data(), Gosh::meetup_where_of( from_at ) );
              to_end( side == 0, answer, meetup_at );
              continue;
            }
            const Gosh::Registrations::Send s
              = meetup.datagram( bytes.data(), bytes.size(), Gosh::meetup_where_of( from_at ), now / 1000, key_of );
            Addr dest;
            socklen_t len;
            if ( s.any && Gosh::meetup_addr_of( s.to, dest, len ) ) {
              if ( s.bytes.size() != Gosh::MEETUP_ADDRESS_MESSAGE ) {
                meetup_passed++;
              }
              if ( is( dest, true ) ) {
                to_end( true, s.bytes, meetup_at );
              } else if ( is( dest, false ) ) {
                to_end( false, s.bytes, meetup_at );
              }
            }
            continue;
          }
          const bool to_other = is( to, side != 0 );
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
    return Network::same_ip_port( where, end_a ? a_at : b_at ) || Network::same_ip_port( where, end_a ? a_at6 : b_at6 );
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

int main()
{
  const std::string key = Crypto::Base64Key().printable_key();
  const Gosh::Ticket ticket = Gosh::make_ticket( MASTER, "random!!", 2000000000 );

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    net.pass( 1000 );
    check( "each told where the other is: a way straight through, both ways (REQ1/PATH-3)",
           a.open( net.now ) && b.open( net.now ) && a.direct() && b.direct() );
    check( "each knows where the other is", a.where() == "198.51.100.2:5000" && b.where() == "203.0.113.1:4000" );
    a.send( "a datagram of the session's" );
    net.deliver();
    check( "a datagram sent goes straight to the other end", net.b_got == "a datagram of the session's" );

    net.straight = false;
    net.pass( PathFinding::OPEN_FOR + 1000 );
    check( "when nothing comes back for 3.5 s, the way is thought closed", !a.open( net.now ) && !b.open( net.now ) );
    net.straight = true;
    net.pass( 2500 );
    check( "and found again when it opens", a.open( net.now ) && b.open( net.now ) );

    a.goodbye();
    net.deliver();
    check( "a goodbye reaches the other end", net.b_goodbyes == 1 );
  }

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.straight = false;
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    check( "a meetup is taken at an address, with a ticket", a.use_meetup( "192.0.2.9:3478", ticket, 1 )
                                                                 && b.use_meetup( "192.0.2.9:3478", ticket, 1 ) );
    check( "and not at what is not one", !a.use_meetup( "meetup.example:1", ticket, 1 ) );
    a.use_meetup( "192.0.2.9:3478", ticket, 1 );
    net.pass( 1000 );
    check( "nothing straight gets through, and both register with the meetup (REQ15/PATH-5)",
           a.meetup_registered() && b.meetup_registered() );
    net.pass( 1000 );
    check( "and find each other through it: open, and not straight (REQ15/PATH-4)",
           a.open( net.now ) && b.open( net.now ) && !a.direct() && !b.direct()
             && a.where() == "the meetup at 192.0.2.9:3478" );
    const int before = net.meetup_passed;
    a.send( "through the meetup" );
    net.deliver();
    check( "a datagram sent goes through the meetup to the other end", net.b_got == "through the meetup" && net.meetup_passed > before );

    net.straight = true;
    net.pass( 2 * PathFinding::SLOW_EVERY + 500 );
    check( "a way straight through is still looked for, and taken when it opens", a.direct() && b.direct() );
    net.b_got.clear();
    const int via = net.meetup_passed;
    a.send( "straight again" );
    net.deliver();
    check( "and then datagrams go straight, not through the meetup", net.b_got == "straight again" && net.meetup_passed == via );
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
    net.to_end( true, Gosh::meetup_passed( probe ), net.meetup_at );
    check( "with a way straight through, a probe heard through the meetup as well does not turn it back",
           !probe.empty() && a.direct() );

    net.straight = false;
    net.a_at = at( "203.0.113.1:4444" ); /* the NAT moves end 0 to another port */
    net.pass( PathFinding::REGISTER_EVERY + PathFinding::OPEN_FOR + 6000 );
    check( "end 0 moved by its NAT: it registers again from where it now is, and the way through holds",
           a.meetup_registered() && a.open( net.now ) && b.open( net.now ) && !a.direct() );
  }

  {
    /* The NAT moves end 0 between its asking the meetup where it is and its registering. */
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.straight = false;
    a.use_meetup( "192.0.2.9:3478", ticket, 1 );
    a.tick( net.now );
    net.deliver(); /* the STUN question and answer: end 0 believes it is at :4000 */
    Crypto::Session liar( Gosh::raw_key( ticket.key ) );
    net.to_end( true, Gosh::meetup_seal( liar, Gosh::MEETUP_REGISTERED, ticket.id, 0, true, 7,
                                         Gosh::meetup_where_v4( 0xCB007101, 9999 ) ), net.meetup_at );
    check( "a REGISTERED that says it saw us somewhere we did not say is not taken", !a.meetup_registered() );
    net.a_at = at( "203.0.113.1:4001" );
    net.pass( 2 * PathFinding::REGISTER_RETRY );
    check( "moved before registering: its REGISTERs are refused", !a.meetup_registered() );
    net.pass( 3 * PathFinding::REGISTER_RETRY + 3000 );
    check( "so it asks again where it is, and registers from there", a.meetup_registered() );
  }

  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    const Gosh::Ticket forged = Gosh::make_ticket( "not the master!!", "random!!", 2000000000 );
    net.straight = false;
    a.use_meetup( "192.0.2.9:3478", forged, 1 );
    b.use_meetup( "192.0.2.9:3478", forged, 1 );
    net.pass( 5000 );
    check( "with a ticket the meetup did not make, neither registers, and no way is found",
           !a.meetup_registered() && !b.meetup_registered() && !a.open( net.now ) );
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
    check( "with no way through, it probes every 250 ms for 8 s (about 32)", fast >= 30 && fast <= 34 );
    check( "and then every 2 s (about 4 in 8 s)", slow >= 3 && slow <= 5 );
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
    check( "a probe-marked datagram of 4,008 bytes is not opened (the fifth security fault)",
           b.receive( net.now, huge.data(), huge.size(), net.a_at, d ) == PathFinding::OTHER );
    std::string short_probe( Network::PUNCH_MARK, sizeof( Network::PUNCH_MARK ) );
    short_probe += "abc";
    check( "nor one too short", b.receive( net.now, short_probe.data(), short_probe.size(), net.a_at, d ) == PathFinding::OTHER );

    /* A probe of end 0's, copied as it went, and sent later from somewhere else. */
    a.goodbye(); /* (any probe will do; this one is easy to find) */
    const std::vector<PathFinding::Outgoing> sent = a.take_outbox();
    const std::string copied = sent.empty() ? std::string() : sent[0].bytes;
    net.to_end( false, copied, net.a_at );
    const std::string before = b.where();
    const Addr onlooker = at( "192.0.2.66:6666" );
    net.to_end( false, copied, onlooker );
    check( "a probe sent again, from elsewhere, does not move the way", b.where() == before );
    std::string sealed_elsewhere( Network::PUNCH_MARK, sizeof( Network::PUNCH_MARK ) );
    sealed_elsewhere += std::string( PathFinding::PROBE_LEN - sizeof( Network::PUNCH_MARK ), 'z' );
    net.to_end( false, sealed_elsewhere, onlooker );
    check( "nor does one not sealed with the key", b.where() == before );
    check( "and a datagram from anywhere but the other end is not the session's",
           b.receive( net.now, "hello", 5, onlooker, d ) == PathFinding::OTHER );

    const std::string answer = Gosh::stun_answer( std::string( "\x00\x01\x00\x00\x21\x12\xa4\x42" "twelve bytes", 20 ).data(),
                                                  0xCB007101, 7777 );
    b.take_local_candidates();
    b.receive( net.now, answer.data(), answer.size(), at( "192.0.2.50:3478" ), d );
    const std::vector<std::string> mine = b.take_local_candidates();
    check( "a STUN server's answer gives a new place where we may be reached",
           mine.size() == 1 && mine[0] == "203.0.113.1:7777" );
  }

  /* REQ16/PATH-8: by IPv6. */
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
    check( "two ends with IPv6 alone find a way straight through by it (REQ16/PATH-8)",
           a.open( net.now ) && a.direct() && a.where() == "[2001:db8::b]:5000" );
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
    check( "and, nothing straight getting through, through a meetup with an IPv6 address alone",
           a.meetup_registered() && b.meetup_registered() && a.open( net.now ) && !a.direct()
             && a.where() == "the meetup at [2001:db8::9]:3478" );
    a.send( "by IPv6, through the meetup" );
    net.deliver();
    check( "and a datagram goes through it", net.b_got == "by IPv6, through the meetup" );
  }
  {
    PathFinding a( true, key, 1000 ), b( false, key, 1000 );
    Net net( a, b );
    net.a_at6 = at( "[2001:db8::a]:4000" );
    net.b_at6 = at( "[2001:db8::b]:5000" );
    a.add_candidate( "198.51.100.2:5000" );
    b.add_candidate( "203.0.113.1:4000" );
    net.pass( 1000 );
    check( "two ends with both families, told only each other's IPv4: by IPv4", a.direct() && a.where() == "198.51.100.2:5000" );
    a.add_candidate( "[2001:db8::b]:5000" );
    b.add_candidate( "[2001:db8::a]:4000" );
    net.pass( 3 * PathFinding::OPEN_EVERY );
    check( "told their IPv6 as well, both move to it, and stay (REQ16/PATH-8: IPv6 first)",
           a.where() == "[2001:db8::b]:5000" && b.where() == "[2001:db8::a]:4000" );
    net.pass( 5 * PathFinding::OPEN_EVERY );
    check( "and IPv4's answers go on without taking them back to it", a.where() == "[2001:db8::b]:5000" );
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
    net.to_end( true, probe, net.b_at );
    check( "a fresh probe from the other end by IPv4 does not take it off IPv6 while that answers",
           !probe.empty() && a.where() == "[2001:db8::b]:5000" );
  }

  if ( failures ) {
    printf( "FAIL REQ1/PATH-3, REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: %d checks of path-finding's logic failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/PATH-3, REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: path-finding's logic\n" );
  return 0;
}
