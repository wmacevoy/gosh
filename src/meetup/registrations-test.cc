/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ18/PATH-5: what a meetup does with REGISTER and PASS (registrations.h), with the real
 * cipher. The first half is the meetup working for the two ends of a ticket. The second is the
 * fork's replay (the old passing-test.cc, test_replay) turned round: each thing an onlooker with
 * a copied datagram could do there is shown refused here.
 */

#include <cstdio>
#include <string>

#include "src/meetup/registrations.h"

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

struct KeyOf
{
  int* asked;
  std::string operator()( const std::string& id ) const
  {
    ( *asked )++;
    return Gosh::ticket_key( MASTER, id );
  }
};

/* One end of a ticket: what it seals, as the code that will use the meetup will. */
struct End
{
  Gosh::Ticket ticket;
  int end;
  std::string where;
  Crypto::Session session;
  uint64_t counter;

  End( const Gosh::Ticket& t, int e, const std::string& w ) : End( t, e, w, t.key ) {}
  End( const Gosh::Ticket& t, int e, const std::string& w, const std::string& key )
    : ticket( t ), end( e ), where( w ), session( Gosh::raw_key( key ) ), counter( 0 )
  {}
  /* A REGISTER, its time by the meetup's clock being at (UTC seconds). */
  std::string reg( uint64_t at )
  {
    return Gosh::meetup_seal( session, Gosh::MEETUP_REGISTER, ticket.id, end, false, counter++, Gosh::meetup_register_text( where, at * 1000 ) );
  }
  std::string pass( const std::string& d ) { return Gosh::meetup_seal( session, Gosh::MEETUP_PASS, ticket.id, end, false, counter++, d ); }
};
}

int main()
{
  using namespace Gosh;
  int asked = 0;
  const KeyOf key_of = { &asked };
  const uint64_t now = 500000;
  const Ticket t = make_ticket( MASTER, "random!!", now + TICKET_LIFE );
  const std::string laptop = meetup_where_v4( 0x0A000001, 40000 );
  const std::string desktop = meetup_where_v4( 0xC0A80001, 50000 );
  const std::string onlooker = meetup_where_v4( 0x01020304, 6666 );
  End a( t, 0, laptop );
  End b( t, 1, desktop );

  Registrations m( now - 1000 );
  std::string bytes = a.reg( now );
  Registrations::Send s = m.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
  check( "end 0 registers: answered REGISTERED, to where it is", s.any && s.to == laptop && s.bytes.size() == 70 );
  MeetupHeader h;
  uint64_t counter;
  std::string seen;
  check( "which end 0 opens, sealed by the meetup, saying where it was seen",
         meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && h.kind == MEETUP_REGISTERED
           && meetup_open( a.session, h, true, counter, seen ) && seen == laptop );
  check( "the ticket is held, its key computed once", m.ticket_count() == 1 && asked == 1 );

  bytes = a.pass( "hello" );
  check( "with the other end not registered, nothing is passed", !m.datagram( bytes.data(), bytes.size(), laptop, now, key_of ).any );

  bytes = b.reg( now );
  check( "end 1 registers", m.datagram( bytes.data(), bytes.size(), desktop, now, key_of ).any );
  check( "and its key is not computed again", asked == 1 );

  bytes = a.pass( "hello" );
  const std::string copied_pass = bytes;
  s = m.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
  check( "end 0's datagram goes to end 1, where it registered, as PASSED",
         s.any && s.to == desktop && s.bytes == meetup_passed( "hello" ) );
  bytes = b.pass( "and back" );
  s = m.datagram( bytes.data(), bytes.size(), desktop, now + 1, key_of );
  check( "and end 1's to end 0", s.any && s.to == laptop && s.bytes == meetup_passed( "and back" ) );

  /* The replay, turned round. */
  const std::string copied_reg = a.reg( now + 2 ); /* copied on its way, and held back from the meetup */
  check( "a copied REGISTER, sent from elsewhere before the real one arrives, is refused",
         !m.datagram( copied_reg.data(), copied_reg.size(), onlooker, now + 2, key_of ).any );
  check( "and the real one, arriving after, is taken",
         m.datagram( copied_reg.data(), copied_reg.size(), laptop, now + 2, key_of ).any );
  bytes = b.pass( "still yours" );
  s = m.datagram( bytes.data(), bytes.size(), desktop, now + 3, key_of );
  check( "and end 0 is still where it was: end 1's datagrams still go to it", s.any && s.to == laptop );
  check( "the same REGISTER sent again from the same place changes nothing",
         !m.datagram( copied_reg.data(), copied_reg.size(), laptop, now + 4, key_of ).any );
  check( "a copied PASS sent again is not passed a second time",
         !m.datagram( copied_pass.data(), copied_pass.size(), laptop, now + 4, key_of ).any );
  bytes = a.pass( "from elsewhere" );
  check( "a PASS from anywhere but where its end registered is not passed",
         !m.datagram( bytes.data(), bytes.size(), onlooker, now + 4, key_of ).any );
  End thief( t, 0, onlooker, "not the key 16by" );
  bytes = thief.reg( now + 4 );
  check( "a REGISTER for the ticket, sealed without its key, is refused",
         !m.datagram( bytes.data(), bytes.size(), onlooker, now + 4, key_of ).any );

  asked = 0;
  const Ticket stranger = make_ticket( "not our master!!", "random!!", now + TICKET_LIFE );
  End x( stranger, 0, onlooker );
  bytes = x.reg( now + 5 );
  check( "a ticket made under another master key is refused", !m.datagram( bytes.data(), bytes.size(), onlooker, now + 5, key_of ).any );
  check( "and not held", m.ticket_count() == 1 );
  asked = 0;
  bytes = x.pass( "anything" );
  check( "a PASS for a ticket not held is dropped without a key computed",
         !m.datagram( bytes.data(), bytes.size(), onlooker, now + 5, key_of ).any && asked == 0 );

  const Ticket old = make_ticket( MASTER, "oldtickt", now );
  End o( old, 0, laptop );
  bytes = o.reg( now );
  check( "a ticket past its time is refused", !m.datagram( bytes.data(), bytes.size(), laptop, now, key_of ).any );

  check( "junk, and what is not one of the messages, is dropped",
         !m.datagram( "hello", 5, onlooker, now, key_of ).any && !m.datagram( bytes.data(), 77, laptop, now, key_of ).any );

  /* Late. */
  const uint64_t later = now + 4 + Registrations::END_IDLE + 1; /* end 1 last registered at now + 3 */
  bytes = a.reg( now + 100 );
  m.datagram( bytes.data(), bytes.size(), laptop, now + 100, key_of );
  bytes = a.pass( "after a long quiet" );
  check( "an end not registered again for two minutes is not passed to",
         !m.datagram( bytes.data(), bytes.size(), laptop, later, key_of ).any );
  bytes = b.reg( now + 10 * Registrations::END_IDLE );
  check( "when both have gone quiet the ticket is let go, and taken again on registering",
         m.datagram( bytes.data(), bytes.size(), desktop, now + 10 * Registrations::END_IDLE, key_of ).any
           && m.ticket_count() == 1 );

  /* REQ17/PATH-8: one end by IPv6, the other by IPv4, through one meetup that has both. */
  {
    Registrations both( now - 1000 );
    const Ticket t6 = make_ticket( MASTER, "ipv6....", now + TICKET_LIFE );
    unsigned char v6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x09 };
    const std::string home6 = meetup_where_v6( v6, 50000 );
    End e6( t6, 1, home6 ), e4( t6, 0, laptop );
    bytes = e6.reg( now );
    check( "an end registers by IPv6 (REQ17/PATH-8)", both.datagram( bytes.data(), bytes.size(), home6, now, key_of ).any );
    bytes = e4.reg( now );
    both.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
    bytes = e4.pass( "to the IPv6 end" );
    s = both.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
    check( "and what the IPv4 end sends goes to it, at its IPv6 address", s.any && s.to == home6 );
    bytes = e6.pass( "back" );
    s = both.datagram( bytes.data(), bytes.size(), home6, now, key_of );
    check( "and the way back", s.any && s.to == laptop );
  }

  /* Full: no new ticket while every one held may still be replayed (REQ18/PATH-5). */
  {
    Registrations full( now - 1000 );
    for ( size_t i = 0; i < Registrations::MAX_TICKETS; i++ ) {
      std::string r( "quiet..." );
      r[7] = static_cast<char>( i );
      const std::string w = meetup_where_v4( 0x0B000000 + i, 2 );
      End q( make_ticket( MASTER, r, now + TICKET_LIFE ), 0, w );
      bytes = q.reg( now );
      full.datagram( bytes.data(), bytes.size(), w, now, key_of );
    }
    check( "64 tickets held", full.ticket_count() == Registrations::MAX_TICKETS );
    const std::string w = meetup_where_v4( 0x0C000001, 3 );
    End late( make_ticket( MASTER, "late....", now + TICKET_LIFE ), 0, w );
    bytes = late.reg( now + 2 );
    check( "a 65th is not taken while every one held has registered within two minutes",
           !full.datagram( bytes.data(), bytes.size(), w, now + 2, key_of ).any && full.ticket_count() == 64 );
    const uint64_t quiet = now + Registrations::END_IDLE + 1;
    bytes = late.reg( quiet );
    check( "and is taken once they have gone quiet",
           full.datagram( bytes.data(), bytes.size(), w, quiet, key_of ).any && full.ticket_count() == 1 );
  }

  /* REQ18/PATH-5: what was copied off the network is no use even after the meetup lets its ticket go. */
  {
    Registrations r( now - 1000 );
    const Ticket t2 = make_ticket( MASTER, "forgets.", now + TICKET_LIFE );
    End c( t2, 0, laptop ), d( t2, 1, desktop );
    const std::string copied_c = c.reg( now );
    s = r.datagram( copied_c.data(), copied_c.size(), laptop, now, key_of );
    uint64_t first_sealed = 0;
    std::string where_seen;
    meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && meetup_open( c.session, h, true, first_sealed, where_seen );
    bytes = d.reg( now );
    r.datagram( bytes.data(), bytes.size(), desktop, now, key_of );
    const std::string copied_p = c.pass( "once" );
    check( "(a ticket held, both ends registered, a PASS passed)",
           r.datagram( copied_p.data(), copied_p.size(), laptop, now, key_of ).any );

    const uint64_t gone = now + Registrations::END_IDLE + 1;
    s = r.datagram( copied_c.data(), copied_c.size(), laptop, gone, key_of );
    check( "once the ticket is let go, the copied REGISTER is not taken again",
           r.ticket_count() == 0 && ( !s.any || ( meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && h.kind == MEETUP_TIME ) ) );
    check( "and the copied PASS is not passed", !r.datagram( copied_p.data(), copied_p.size(), laptop, gone, key_of ).any );

    bytes = c.reg( gone );
    s = r.datagram( bytes.data(), bytes.size(), laptop, gone, key_of );
    uint64_t again_sealed = 0;
    check( "the end itself registers again, and is taken",
           s.any && meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && h.kind == MEETUP_REGISTERED
             && meetup_open( c.session, h, true, again_sealed, where_seen ) );
    check( "and what the meetup seals for the ticket taken again is not numbered as before (a nonce once only)",
           again_sealed > first_sealed );
    bytes = d.reg( gone );
    r.datagram( bytes.data(), bytes.size(), desktop, gone, key_of );
    check( "a PASS sealed before that REGISTER is not passed, though the ticket is held afresh",
           !r.datagram( copied_p.data(), copied_p.size(), laptop, gone, key_of ).any );
    bytes = c.pass( "new" );
    check( "while a new one is", r.datagram( bytes.data(), bytes.size(), laptop, gone, key_of ).any );
  }

  /* REQ18/PATH-5: an end whose clock is wrong is told the meetup's, and only it can read that. */
  {
    Registrations r( now - 1000 );
    const Ticket t3 = make_ticket( MASTER, "clock...", now + TICKET_LIFE );
    End c( t3, 0, laptop );
    const uint64_t asked = c.counter;
    bytes = c.reg( now - 600 );
    s = r.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
    std::string text;
    uint64_t answering = 0, its_time = 0;
    check( "a REGISTER ten minutes behind the meetup's clock is not taken", r.ticket_count() == 0 );
    check( "it is answered TIME, sealed for that end, with the meetup's time and the REGISTER it answers",
           s.any && s.to == laptop && meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && h.kind == MEETUP_TIME
             && meetup_open( c.session, h, true, counter, text ) && meetup_read_time( text, answering, its_time )
             && answering == asked && its_time == now * 1000 );
    bytes = c.reg( now + 600 );
    check( "and so is one ten minutes ahead", r.datagram( bytes.data(), bytes.size(), laptop, now, key_of ).any && r.ticket_count() == 0 );
    bytes = c.reg( now + Registrations::CLOCK_SLACK );
    check( "one within a minute is taken", r.datagram( bytes.data(), bytes.size(), laptop, now, key_of ).any && r.ticket_count() == 1 );
    End thief( t3, 0, laptop, "not the key 16by" );
    bytes = thief.reg( now - 600 );
    check( "a REGISTER not sealed with the ticket's key is told nothing",
           !r.datagram( bytes.data(), bytes.size(), laptop, now, key_of ).any );
    End elsewhere( t3, 0, onlooker );
    bytes = elsewhere.reg( now - 600 );
    check( "nor one from anywhere but where it says it is", !r.datagram( bytes.data(), bytes.size(), laptop, now, key_of ).any );
  }

  /* Not closed (README): a meetup just started remembers nothing, so a REGISTER copied in the minute
     before is taken again in the minute after. Shown here, so that closing it shows too. */
  {
    Registrations before( now - 1000 ), after( now );
    const Ticket t4 = make_ticket( MASTER, "restart.", now + TICKET_LIFE );
    End c( t4, 0, laptop );
    bytes = c.reg( now );
    before.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
    check( "(not closed) a meetup just started takes again a REGISTER copied the minute before",
           after.datagram( bytes.data(), bytes.size(), laptop, now + 30, key_of ).any );
  }

  if ( failures ) {
    printf( "FAIL REQ18/PATH-5, REQ17/PATH-8: %d checks of the meetup's registrations failed\n", failures );
    return 1;
  }
  printf( "PASS REQ18/PATH-5, REQ17/PATH-8: the meetup's registrations\n" );
  return 0;
}
