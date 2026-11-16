/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ15/PATH-5: what a meetup does with REGISTER and PASS (registrations.h), with the real
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
  std::string reg( void ) { return Gosh::meetup_seal( session, Gosh::MEETUP_REGISTER, ticket.id, end, false, counter++, where ); }
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

  Registrations m;
  std::string bytes = a.reg();
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

  bytes = b.reg();
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
  const std::string copied_reg = a.reg(); /* copied on its way, and held back from the meetup */
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
  bytes = thief.reg();
  check( "a REGISTER for the ticket, sealed without its key, is refused",
         !m.datagram( bytes.data(), bytes.size(), onlooker, now + 4, key_of ).any );

  asked = 0;
  const Ticket stranger = make_ticket( "not our master!!", "random!!", now + TICKET_LIFE );
  End x( stranger, 0, onlooker );
  bytes = x.reg();
  check( "a ticket made under another master key is refused", !m.datagram( bytes.data(), bytes.size(), onlooker, now + 5, key_of ).any );
  check( "and not held", m.ticket_count() == 1 );
  asked = 0;
  bytes = x.pass( "anything" );
  check( "a PASS for a ticket not held is dropped without a key computed",
         !m.datagram( bytes.data(), bytes.size(), onlooker, now + 5, key_of ).any && asked == 0 );

  const Ticket old = make_ticket( MASTER, "oldtickt", now );
  End o( old, 0, laptop );
  bytes = o.reg();
  check( "a ticket past its time is refused", !m.datagram( bytes.data(), bytes.size(), laptop, now, key_of ).any );

  check( "junk, and what is not one of the messages, is dropped",
         !m.datagram( "hello", 5, onlooker, now, key_of ).any && !m.datagram( bytes.data(), 69, laptop, now, key_of ).any );

  /* Late. */
  const uint64_t later = now + 4 + Registrations::END_IDLE + 1; /* end 1 last registered at now + 3 */
  bytes = a.reg();
  m.datagram( bytes.data(), bytes.size(), laptop, now + 100, key_of );
  bytes = a.pass( "after a long quiet" );
  check( "an end not registered again for two minutes is not passed to",
         !m.datagram( bytes.data(), bytes.size(), laptop, later, key_of ).any );
  bytes = b.reg();
  check( "when both have gone quiet the ticket is let go, and taken again on registering",
         m.datagram( bytes.data(), bytes.size(), desktop, now + 10 * Registrations::END_IDLE, key_of ).any
           && m.ticket_count() == 1 );

  /* REQ17/PATH-8: one end by IPv6, the other by IPv4, through one meetup that has both. */
  {
    Registrations both;
    const Ticket t6 = make_ticket( MASTER, "ipv6....", now + TICKET_LIFE );
    unsigned char v6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x09 };
    const std::string home6 = meetup_where_v6( v6, 50000 );
    End e6( t6, 1, home6 ), e4( t6, 0, laptop );
    bytes = e6.reg();
    check( "an end registers by IPv6 (REQ17/PATH-8)", both.datagram( bytes.data(), bytes.size(), home6, now, key_of ).any );
    bytes = e4.reg();
    both.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
    bytes = e4.pass( "to the IPv6 end" );
    s = both.datagram( bytes.data(), bytes.size(), laptop, now, key_of );
    check( "and what the IPv4 end sends goes to it, at its IPv6 address", s.any && s.to == home6 );
    bytes = e6.pass( "back" );
    s = both.datagram( bytes.data(), bytes.size(), home6, now, key_of );
    check( "and the way back", s.any && s.to == laptop );
  }

  /* Full. */
  Registrations full;
  const std::string busy_where = meetup_where_v4( 0x0A000063, 1 );
  const Ticket busy = make_ticket( MASTER, "busy....", now + TICKET_LIFE );
  End b0( busy, 0, busy_where ), b1( busy, 1, busy_where );
  bytes = b0.reg();
  full.datagram( bytes.data(), bytes.size(), busy_where, now, key_of );
  bytes = b1.reg();
  full.datagram( bytes.data(), bytes.size(), busy_where, now, key_of );
  for ( size_t i = 1; i < Registrations::MAX_TICKETS; i++ ) {
    std::string r( "quiet..." );
    r[7] = static_cast<char>( i );
    const std::string w = meetup_where_v4( 0x0B000000 + i, 2 );
    End q( make_ticket( MASTER, r, now + TICKET_LIFE ), 0, w );
    bytes = q.reg();
    full.datagram( bytes.data(), bytes.size(), w, now, key_of );
  }
  bytes = b0.pass( "in use" );
  full.datagram( bytes.data(), bytes.size(), busy_where, now + 1, key_of );
  check( "64 tickets held", full.ticket_count() == Registrations::MAX_TICKETS );
  const std::string w = meetup_where_v4( 0x0C000001, 3 );
  End late( make_ticket( MASTER, "late....", now + TICKET_LIFE ), 0, w );
  bytes = late.reg();
  check( "a 65th is taken", full.datagram( bytes.data(), bytes.size(), w, now + 2, key_of ).any && full.ticket_count() == 64 );
  bytes = b1.pass( "still in use" );
  check( "and the one in use is not the one put out",
         full.datagram( bytes.data(), bytes.size(), busy_where, now + 2, key_of ).any );

  if ( failures ) {
    printf( "FAIL REQ15/PATH-5, REQ17/PATH-8: %d checks of the meetup's registrations failed\n", failures );
    return 1;
  }
  printf( "PASS REQ15/PATH-5, REQ17/PATH-8: the meetup's registrations\n" );
  return 0;
}
