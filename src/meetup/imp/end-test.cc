/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ18/PATH-5: meetup's end (end.h), with the meetup's logic (logic.h), and with a meetup that lies. */

#include <cstdio>
#include <optional>
#include <string>

#include "src/meetup/end.h"
#include "src/meetup/logic.h"
#include "vendor/facts/facts.h"

using Network::Addr;

namespace {
const std::string MASTER( "0123456789abcdef" );

/* The meetup's clock: UTC seconds, far from the end's (whose time of day is given as 1 ms), so that it must learn it. */
const uint64_t MEETUP_CLOCK = 1000000;

Addr at( const std::string& ip_port )
{
  Addr a;
  Gosh::Stun::parse_ip_port( ip_port, a );
  return a;
}

/* One end and a meetup; what the end sends reaches the meetup from `seen`, where the end's NAT shows it. */
struct Net
{
  Gosh::Meetup::End& end;
  Gosh::Meetup::Logic meetup;
  Addr meetup_at, seen;
  uint64_t now; /* ms */
  int told_time;

  explicit Net( Gosh::Meetup::End& e )
    : end( e ), meetup( MEETUP_CLOCK - 1000, MASTER ), meetup_at( at( "192.0.2.9:3478" ) ), seen( at( "203.0.113.1:4000" ) ), now( 1000 ), told_time( 0 )
  {}

  void to_meetup( const std::string& bytes )
  {
    const Gosh::Meetup::Logic::Send s = meetup.heard( bytes.data(), bytes.size(), seen, MEETUP_CLOCK + now / 1000 );
    if ( !s.any || s.to_len == 0 || !Gosh::Stun::same_ip_port( s.to, seen ) ) {
      return;
    }
    if ( s.stun ) {
      Addr mapped;
      if ( Gosh::Stun::stun_parse( s.bytes.data(), s.bytes.size(), mapped ) ) {
        end.stun_answer( meetup_at, mapped );
      }
      return;
    }
    Gosh::Meetup::Imp::MeetupHeader h;
    told_time += Gosh::Meetup::Imp::meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && h.kind == Gosh::Meetup::Imp::MEETUP_TIME;
    std::string passed;
    end.heard( meetup_at, s.bytes.data(), s.bytes.size(), now, passed );
  }

  /* Time passes, in steps of 50 ms, the end ticking and what it sends delivered. */
  void pass( uint64_t ms )
  {
    for ( uint64_t t = 0; t < ms; t += 50 ) {
      now += 50;
      const std::string out = end.tick( now );
      if ( !out.empty() ) {
        to_meetup( out );
      }
    }
  }
};
}

FACTS( End )
{
  const Gosh::Meetup::Ticket ticket = Gosh::Meetup::make_ticket( MASTER, "random!!", 2000000000 );

  {
    Gosh::Meetup::End end( true );
    Net net( end );
    FACT_CHECK( ( end.use( "192.0.2.9:3478", ticket, 1 ) && end.in_use() ), "a meetup is taken at an address, with a ticket" );
    net.pass( 1000 );
    FACT_CHECK( ( end.registered() && net.told_time == 1 ), "it registers, by the meetup's clock, which it was told (TIME), its own being far off" );
  }

  /* A TIME is believed only as the answer to our last REGISTER, so an old one sent again moves no clock. */
  {
    Gosh::Meetup::End end( true );
    Net net( end );
    end.use( "192.0.2.9:3478", ticket, 1 );
    net.pass( 1000 );
    Crypto::Session as_meetup( Gosh::Meetup::raw_key( ticket.key ) );
    const uint64_t wrong = ( MEETUP_CLOCK + 86400 ) * 1000; /* a day ahead */
    const std::string old_time = Gosh::Meetup::Imp::meetup_seal( as_meetup, Gosh::Meetup::Imp::MEETUP_TIME, ticket.id, 0, true, 99999, Gosh::Meetup::Imp::meetup_time_text( 7, wrong ) );
    std::string passed;
    end.heard( net.meetup_at, old_time.data(), old_time.size(), net.now, passed );
    std::string reg;
    for ( int i = 0; i < 1000 && reg.empty(); i++ ) {
      net.now += 50;
      const std::string out = end.tick( net.now );
      Gosh::Meetup::Imp::MeetupHeader h;
      if ( Gosh::Meetup::Imp::meetup_read_header( out.data(), out.size(), h ) && h.kind == Gosh::Meetup::Imp::MEETUP_REGISTER ) {
        reg = out;
      }
    }
    Gosh::Meetup::Imp::MeetupHeader h;
    uint64_t said = 0;
    std::optional<Gosh::Meetup::Imp::Unsealed<Strong::Meetup::Register>> text;
    const bool opened = Gosh::Meetup::Imp::meetup_read_header( reg.data(), reg.size(), h )
                        && Gosh::Meetup::Imp::Sealed<Strong::Meetup::Register, Gosh::Meetup::Imp::MeetupSealer>( h ).unseal( Gosh::Meetup::Imp::MeetupSealer{ as_meetup, false }, text )
                        && Gosh::Meetup::Imp::meetup_read_register( *text, said );
    const uint64_t meetup_ms = ( MEETUP_CLOCK + net.now / 1000 ) * 1000;
    FACT_CHECK( ( end.registered() && opened && said + 5000 > meetup_ms && said < meetup_ms + 5000 ), "a TIME that answers no REGISTER of ours is not believed: the next REGISTER keeps the meetup's clock" );
  }

  /* The NAT moves the end between its asking the meetup where it is and its registering. */
  {
    Gosh::Meetup::End end( true );
    Net net( end );
    end.use( "192.0.2.9:3478", ticket, 1 );
    net.to_meetup( end.tick( net.now ) ); /* the STUN question and answer: it believes it is at :4000 */
    Crypto::Session liar( Gosh::Meetup::raw_key( ticket.key ) );
    const std::string lie = Gosh::Meetup::Imp::meetup_seal( liar, Gosh::Meetup::Imp::MEETUP_REGISTERED, ticket.id, 0, true, 7, Gosh::Meetup::Imp::meetup_where_v4( 0xCB007101, 9999 ) );
    std::string passed;
    end.heard( net.meetup_at, lie.data(), lie.size(), net.now, passed );
    FACT_CHECK( !end.registered(), "a REGISTERED that says it saw us somewhere we did not say is not taken" );
    net.seen = at( "203.0.113.1:4001" );
    net.pass( 2 * Gosh::Meetup::End::REGISTER_RETRY );
    FACT_CHECK( !end.registered(), "moved before registering: its REGISTERs are refused" );
    net.pass( 3 * Gosh::Meetup::End::REGISTER_RETRY + 3000 ); /* (a literal 3, not REGISTER_TRIES: a test that reads the number would pass whatever it was) */
    FACT_CHECK( end.registered(), "so, after three of them, it asks again where it is, and registers from there" );
  }
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( End );
}

FACTS_MAIN
