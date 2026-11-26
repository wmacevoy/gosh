/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ18/PATH-5: what a meetup does with REGISTER and PASS (registrations.h), with the real
 * cipher. The first group is the meetup working for the two ends of a ticket, then the fork's
 * replay (the old passing-test.cc, test_replay) turned round: each thing an onlooker with a copied
 * datagram could do there is shown refused. REQ17/PATH-8: one end by IPv6.
 *
 * Written with facts (vendor/facts/README.md), the trial of 2026-11-17: each fact's note is what
 * it shows, printed with both values when it is false. Run with --facts_plain to read it.
 */

#include <string>

#include "src/meetup/imp/registrations.h"
#include "vendor/facts/facts.h"

using namespace Meetup;
using namespace Meetup::Imp;

namespace {
const std::string MASTER( "0123456789abcdef" );
const uint64_t now = 500000;
const std::string laptop = meetup_where_v4( 0x0A000001, 40000 );
const std::string desktop = meetup_where_v4( 0xC0A80001, 50000 );
const std::string onlooker = meetup_where_v4( 0x01020304, 6666 );

/* The meetup's way to a ticket's key, counting how often it is asked. */
struct KeyOf
{
  int* asked;
  std::string operator()( const std::string& id ) const
  {
    ( *asked )++;
    return Meetup::ticket_key( MASTER, id );
  }
};

/* One end of a ticket: what it seals, as the code that will use the meetup will. */
struct End
{
  Meetup::Ticket ticket;
  int end;
  std::string where;
  Crypto::Session session;
  uint64_t counter;

  End( const Meetup::Ticket& t, int e, const std::string& w ) : End( t, e, w, std::string( t.key ) ) {}
  End( const Meetup::Ticket& t, int e, const std::string& w, const std::string& key )
    : ticket( t ), end( e ), where( w ), session( Meetup::raw_key( key ) ), counter( 0 )
  {}
  /* A REGISTER, its time by the meetup's clock being at (UTC seconds). */
  std::string reg( uint64_t at )
  {
    return Meetup::Imp::meetup_seal( session, Meetup::Imp::MEETUP_REGISTER, ticket.id, end, false, counter++, Meetup::Imp::meetup_register_text( where, at * 1000 ) );
  }
  std::string pass( const std::string& d ) { return Meetup::Imp::meetup_seal( session, Meetup::Imp::MEETUP_PASS, ticket.id, end, false, counter++, d ); }
};

/* What the meetup does with a datagram, as one call. */
Registrations::Send to( Registrations& m, const std::string& bytes, const std::string& from, uint64_t at, const KeyOf& key_of )
{
  return m.datagram( bytes.data(), bytes.size(), from, at, key_of );
}

/* The kind of what the meetup sent, or -1 if it is not one of its messages. */
int kind_of( const Registrations::Send& s )
{
  MeetupHeader h;
  return meetup_read_header( s.bytes.data(), s.bytes.size(), h ) ? h.kind : -1;
}
}

/* REQ18/PATH-5: a ticket's two ends, and the replay turned round. */
FACTS( TwoEndsAndTheReplay )
{
  int asked = 0;
  const KeyOf key_of = { &asked };
  const Ticket t = make_ticket( MASTER, "random!!", now + TICKET_LIFE );
  End a( t, 0, laptop );
  End b( t, 1, desktop );

  Registrations m( now - 1000 );
  Registrations::Send s = to( m, a.reg( now ), laptop, now, key_of );
  FACT( s.any, "end 0 registers: answered" );
  FACT( s.to == laptop, "REGISTERED goes to where end 0 is" );
  FACT( s.bytes.size() == 70u, "REGISTERED is 70 bytes" );
  MeetupHeader h;
  std::optional<Unsealed<Strong::Meetup::Registered>> seen;
  FACT( kind_of( s ) == MEETUP_REGISTERED );
  meetup_read_header( s.bytes.data(), s.bytes.size(), h );
  FACT( ( Sealed<Strong::Meetup::Registered, MeetupSealer>( h ).unseal( MeetupSealer{ a.session, true }, seen ) ), "end 0 opens it: sealed by the meetup" );
  FACT( meetup_registered_at( *seen, laptop ), "saying where end 0 was seen" );
  FACT( m.ticket_count() == 1u, "the ticket is held" );
  FACT( asked == 1, "its key computed once" );

  FACT( !to( m, a.pass( "hello" ), laptop, now, key_of ).any, "with the other end not registered, nothing is passed" );

  FACT( to( m, b.reg( now ), desktop, now, key_of ).any, "end 1 registers" );
  FACT( asked == 1, "and its key is not computed again" );

  const std::string copied_pass = a.pass( "hello" );
  s = to( m, copied_pass, laptop, now, key_of );
  FACT( s.to == desktop, "end 0's datagram goes to end 1, where it registered" );
  FACT( s.bytes == meetup_passed( "hello" ), "as PASSED" );
  s = to( m, b.pass( "and back" ), desktop, now + 1, key_of );
  FACT( s.to == laptop, "and end 1's to end 0" );
  FACT( s.bytes == meetup_passed( "and back" ) );

  /* The replay, turned round. */
  const std::string copied_reg = a.reg( now + 2 ); /* copied on its way, and held back from the meetup */
  FACT( !to( m, copied_reg, onlooker, now + 2, key_of ).any, "a copied REGISTER, sent from elsewhere before the real one arrives, is refused" );
  FACT( to( m, copied_reg, laptop, now + 2, key_of ).any, "and the real one, arriving after, is taken" );
  s = to( m, b.pass( "still yours" ), desktop, now + 3, key_of );
  FACT( s.to == laptop, "and end 0 is still where it was: end 1's datagrams still go to it" );
  FACT( !to( m, copied_reg, laptop, now + 4, key_of ).any, "the same REGISTER sent again from the same place changes nothing" );
  FACT( !to( m, copied_pass, laptop, now + 4, key_of ).any, "a copied PASS sent again is not passed a second time" );
  FACT( !to( m, a.pass( "from elsewhere" ), onlooker, now + 4, key_of ).any, "a PASS from anywhere but where its end registered is not passed" );
  End thief( t, 0, onlooker, "not the key 16by" );
  FACT( !to( m, thief.reg( now + 4 ), onlooker, now + 4, key_of ).any, "a REGISTER for the ticket, sealed without its key, is refused" );

  const Ticket stranger = make_ticket( "not our master!!", "random!!", now + TICKET_LIFE );
  End x( stranger, 0, onlooker );
  FACT( !to( m, x.reg( now + 5 ), onlooker, now + 5, key_of ).any, "a ticket made under another master key is refused" );
  FACT( m.ticket_count() == 1u, "and not held" );
  asked = 0;
  FACT( !to( m, x.pass( "anything" ), onlooker, now + 5, key_of ).any, "a PASS for a ticket not held is dropped" );
  FACT( asked == 0, "without a key computed" );

  End o( make_ticket( MASTER, "oldtickt", now ), 0, laptop );
  const std::string old_reg = o.reg( now );
  FACT( !to( m, old_reg, laptop, now, key_of ).any, "a ticket past its time is refused" );

  FACT( !m.datagram( "hello", 5, onlooker, now, key_of ).any, "junk is dropped" );
  FACT( !m.datagram( old_reg.data(), 77, laptop, now, key_of ).any, "and what is not one of the messages" );

  /* Late. */
  const uint64_t later = now + 4 + Registrations::END_IDLE + 1; /* end 1 last registered at now + 3 */
  to( m, a.reg( now + 100 ), laptop, now + 100, key_of );
  FACT( !to( m, a.pass( "after a long quiet" ), laptop, later, key_of ).any, "an end not registered again for two minutes is not passed to" );
  FACT( to( m, b.reg( now + 10 * Registrations::END_IDLE ), desktop, now + 10 * Registrations::END_IDLE, key_of ).any,
        "when both have gone quiet the ticket is let go, and taken again on registering" );
  FACT( m.ticket_count() == 1u );
}

/* REQ17/PATH-8: one end by IPv6, the other by IPv4, through one meetup that has both. */
FACTS( OneEndByIPv6 )
{
  int asked = 0;
  const KeyOf key_of = { &asked };
  Registrations both( now - 1000 );
  const Ticket t6 = make_ticket( MASTER, "ipv6....", now + TICKET_LIFE );
  unsigned char v6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x09 };
  const std::string home6 = meetup_where_v6( v6, 50000 );
  End e6( t6, 1, home6 ), e4( t6, 0, laptop );
  FACT( to( both, e6.reg( now ), home6, now, key_of ).any, "an end registers by IPv6" );
  to( both, e4.reg( now ), laptop, now, key_of );
  Registrations::Send s = to( both, e4.pass( "to the IPv6 end" ), laptop, now, key_of );
  FACT( s.to == home6, "what the IPv4 end sends goes to it, at its IPv6 address" );
  s = to( both, e6.pass( "back" ), home6, now, key_of );
  FACT( s.to == laptop, "and the way back" );
}

/* REQ18/PATH-5: no new ticket while every one held may still be replayed. */
FACTS( FullTable )
{
  int asked = 0;
  const KeyOf key_of = { &asked };
  Registrations full( now - 1000 );
  for ( size_t i = 0; i < Registrations::MAX_TICKETS; i++ ) {
    std::string r( "quiet..." );
    r[7] = static_cast<char>( i );
    const std::string w = meetup_where_v4( 0x0B000000 + i, 2 );
    End q( make_ticket( MASTER, r, now + TICKET_LIFE ), 0, w );
    to( full, q.reg( now ), w, now, key_of );
  }
  FACT( full.ticket_count() == Registrations::MAX_TICKETS, "64 tickets held" );
  const std::string w = meetup_where_v4( 0x0C000001, 3 );
  End late( make_ticket( MASTER, "late....", now + TICKET_LIFE ), 0, w );
  FACT( !to( full, late.reg( now + 2 ), w, now + 2, key_of ).any, "a 65th is not taken while every one held has registered within two minutes" );
  FACT( full.ticket_count() == 64u );
  const uint64_t quiet = now + Registrations::END_IDLE + 1;
  FACT( to( full, late.reg( quiet ), w, quiet, key_of ).any, "and is taken once they have gone quiet" );
  FACT( full.ticket_count() == 1u );
}

/* REQ18/PATH-5: what was copied off the network is no use even after the meetup lets its ticket go. */
FACTS( CopiesAfterLettingGo )
{
  int asked = 0;
  const KeyOf key_of = { &asked };
  Registrations r( now - 1000 );
  const Ticket t2 = make_ticket( MASTER, "forgets.", now + TICKET_LIFE );
  End c( t2, 0, laptop ), d( t2, 1, desktop );
  const std::string copied_c = c.reg( now );
  Registrations::Send s = to( r, copied_c, laptop, now, key_of );
  MeetupHeader h;
  std::optional<Unsealed<Strong::Meetup::Registered>> seen;
  meetup_read_header( s.bytes.data(), s.bytes.size(), h )
    && Sealed<Strong::Meetup::Registered, MeetupSealer>( h ).unseal( MeetupSealer{ c.session, true }, seen );
  const uint64_t first_sealed = seen ? seen->counter() : 0;
  to( r, d.reg( now ), desktop, now, key_of );
  const std::string copied_p = c.pass( "once" );
  FACT( to( r, copied_p, laptop, now, key_of ).any, "(a ticket held, both ends registered, a PASS passed)" );

  const uint64_t gone = now + Registrations::END_IDLE + 1;
  s = to( r, copied_c, laptop, gone, key_of );
  FACT( r.ticket_count() == 0u, "once the ticket is let go, the copied REGISTER is not taken again" );
  FACT( ( !s.any || kind_of( s ) == MEETUP_TIME ), "at most it is answered TIME" );
  FACT( !to( r, copied_p, laptop, gone, key_of ).any, "and the copied PASS is not passed" );

  s = to( r, c.reg( gone ), laptop, gone, key_of );
  FACT( kind_of( s ) == MEETUP_REGISTERED, "the end itself registers again, and is taken" );
  meetup_read_header( s.bytes.data(), s.bytes.size(), h );
  FACT( ( Sealed<Strong::Meetup::Registered, MeetupSealer>( h ).unseal( MeetupSealer{ c.session, true }, seen ) ) );
  FACT( seen->counter() > first_sealed, "what the meetup seals for the ticket taken again is not numbered as before (a nonce once only)" );
  to( r, d.reg( gone ), desktop, gone, key_of );
  FACT( !to( r, copied_p, laptop, gone, key_of ).any, "a PASS sealed before that REGISTER is not passed, though the ticket is held afresh" );
  FACT( to( r, c.pass( "new" ), laptop, gone, key_of ).any, "while a new one is" );
}

/* REQ18/PATH-5: an end whose clock is wrong is told the meetup's, and only it can read that. */
FACTS( WrongClock )
{
  int asked = 0;
  const KeyOf key_of = { &asked };
  Registrations r( now - 1000 );
  const Ticket t3 = make_ticket( MASTER, "clock...", now + TICKET_LIFE );
  End c( t3, 0, laptop );
  const uint64_t asking = c.counter;
  Registrations::Send s = to( r, c.reg( now - 600 ), laptop, now, key_of );
  FACT( r.ticket_count() == 0u, "a REGISTER ten minutes behind the meetup's clock is not taken" );
  FACT( s.to == laptop, "it is answered, to that end" );
  FACT( kind_of( s ) == MEETUP_TIME, "with TIME" );
  MeetupHeader h;
  meetup_read_header( s.bytes.data(), s.bytes.size(), h );
  uint64_t answering = 0, its_time = 0;
  std::optional<Unsealed<Strong::Meetup::Time>> told;
  FACT( ( Sealed<Strong::Meetup::Time, MeetupSealer>( h ).unseal( MeetupSealer{ c.session, true }, told ) ), "sealed for that end" );
  FACT( meetup_read_time( *told, answering, its_time ) );
  FACT( answering == asking, "naming the REGISTER it answers" );
  FACT( its_time == now * 1000, "with the meetup's time" );
  FACT( to( r, c.reg( now + 600 ), laptop, now, key_of ).any, "and so is one ten minutes ahead" );
  FACT( r.ticket_count() == 0u );
  FACT( to( r, c.reg( now + Registrations::CLOCK_SLACK ), laptop, now, key_of ).any, "one within a minute is taken" );
  FACT( r.ticket_count() == 1u );
  End thief( t3, 0, laptop, "not the key 16by" );
  FACT( !to( r, thief.reg( now - 600 ), laptop, now, key_of ).any, "a REGISTER not sealed with the ticket's key is told nothing" );
  End elsewhere( t3, 0, onlooker );
  FACT( !to( r, elsewhere.reg( now - 600 ), laptop, now, key_of ).any, "nor one from anywhere but where it says it is" );
}

/* Not closed (README): a meetup just started remembers nothing, so a REGISTER copied in the minute
   before is taken again in the minute after. Shown here, so that closing it shows too. */
FACTS( NotClosedRestart )
{
  int asked = 0;
  const KeyOf key_of = { &asked };
  Registrations before( now - 1000 ), after( now );
  End c( make_ticket( MASTER, "restart.", now + TICKET_LIFE ), 0, laptop );
  const std::string bytes = c.reg( now );
  to( before, bytes, laptop, now, key_of );
  FACT( to( after, bytes, laptop, now + 30, key_of ).any, "(not closed) a meetup just started takes again a REGISTER copied the minute before" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( TwoEndsAndTheReplay );
  FACTS_REGISTER( OneEndByIPv6 );
  FACTS_REGISTER( FullTable );
  FACTS_REGISTER( CopiesAfterLettingGo );
  FACTS_REGISTER( WrongClock );
  FACTS_REGISTER( NotClosedRestart );
}

FACTS_MAIN
