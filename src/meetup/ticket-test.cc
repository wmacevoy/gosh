/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ18/PATH-5: a ticket (ticket.h), with the real cipher: its key comes only from the master key
 * and the whole id, it stops being good when its id says, and its text goes there and back.
 */

#include <cstdio>
#include <string>

#include "src/meetup/ticket.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  using namespace Gosh;
  const std::string master( "0123456789abcdef" );
  const std::string other_master( "fedcba9876543210" );
  const Ticket t = make_ticket( master, "random!!", 1000000 );

  check( "an id is 16 bytes, a key 16", t.id.size() == 16 && t.key.size() == 16 );
  check( "the id says when it stops being good", ticket_until( t.id ) == 1000000 );
  check( "good before that instant", ticket_good_at( t.id, 999999 ) );
  check( "and not at it, or after", !ticket_good_at( t.id, 1000000 ) && !ticket_good_at( t.id, 2000000 ) );
  check( "the meetup computes the same key from the id alone", ticket_key( master, t.id ) == t.key );
  check( "another master key gives another key", ticket_key( other_master, t.id ) != t.key );

  bool all_differ = true;
  for ( size_t i = 0; i < 16; i++ ) {
    std::string changed = t.id;
    changed[i] ^= 0x01;
    all_differ = all_differ && ticket_key( master, changed ) != t.key;
  }
  check( "an id changed in any one bit, its time included, gives another key", all_differ );
  check( "the key is not the id", t.key != t.id );

  check( "an id or a master key of the wrong size gives no key",
         ticket_key( master, "short" ).empty() && ticket_key( "short", t.id ).empty() );
  check( "what is not an id is good at no time", !ticket_good_at( "short", 0 ) );

  Ticket back;
  const std::string text = ticket_text( t );
  check( "as text: \"gosh-ticket\", then two keys of 22 letters", text.size() == 57 && text.compare( 0, 12, "gosh-ticket " ) == 0 );
  check( "and back again, the same", parse_ticket( text, back ) && back.id == t.id && back.key == t.key );
  check( "text that is not one is refused",
         !parse_ticket( "", back ) && !parse_ticket( text.substr( 0, 56 ), back ) && !parse_ticket( text + "x", back )
           && !parse_ticket( "gosh-tickex" + text.substr( 11 ), back )
           && !parse_ticket( text.substr( 0, 34 ) + "!" + text.substr( 35 ), back ) );

  if ( failures ) {
    printf( "FAIL REQ18/PATH-5: %d checks of tickets failed\n", failures );
    return 1;
  }
  printf( "PASS REQ18/PATH-5: tickets\n" );
  return 0;
}
