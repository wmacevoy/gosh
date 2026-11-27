/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ18/PATH-5: a ticket (ticket.h), with the real cipher: its key comes only from the master key
 * and the whole id, it stops being good when its id says, and its text goes there and back.
 */

#include <cstdio>
#include <string>

#include "src/meetup/ticket.h"
#include "vendor/facts/facts.h"
/* REQ18/PATH-5: tickets */
FACTS( Ticket )
{
  using namespace Gosh::Meetup;
  const std::string master( "0123456789abcdef" );
  const std::string other_master( "fedcba9876543210" );
  const Ticket t = make_ticket( master, "random!!", 1000000 );

  FACT_CHECK( ( t.id.size() == 16 && t.key.size() == 16 ), "an id is 16 bytes, a key 16" );
  FACT_CHECK( ticket_until( t.id ) == 1000000, "the id says when it stops being good" );
  FACT_CHECK( ticket_good_at( t.id, 999999 ), "good before that instant" );
  FACT_CHECK( ( !ticket_good_at( t.id, 1000000 ) && !ticket_good_at( t.id, 2000000 ) ), "and not at it, or after" );
  FACT_CHECK( ticket_key( master, t.id ) == t.key, "the meetup computes the same key from the id alone" );
  FACT_CHECK( ticket_key( other_master, t.id ) != t.key, "another master key gives another key" );

  bool all_differ = true;
  for ( size_t i = 0; i < 16; i++ ) {
    std::string changed = t.id;
    changed[i] ^= 0x01;
    all_differ = all_differ && ticket_key( master, changed ) != t.key;
  }
  FACT_CHECK( all_differ, "an id changed in any one bit, its time included, gives another key" );
  FACT_CHECK( t.key != t.id, "the key is not the id" );

  FACT_CHECK( ( ticket_key( master, "short" ).empty() && ticket_key( "short", t.id ).empty() ), "an id or a master key of the wrong size gives no key" );
  FACT_CHECK( !ticket_good_at( "short", 0 ), "what is not an id is good at no time" );

  FACT_CHECK( raw_key( master ).printable_key() == "MDEyMzQ1Njc4OWFiY2RlZg", "a key is its 16 bytes as they are" );
  bool refused = false;
  try {
    raw_key( "fifteen bytes.." );
  } catch ( const Crypto::CryptoException& ) {
    refused = true;
  }
  FACT_CHECK( refused, "and what is not 16 bytes is no key" );

  Ticket back;
  const std::string text = ticket_text( t );
  FACT_CHECK( ( text.size() == 57 && text.compare( 0, 12, "gosh-ticket " ) == 0 ), "as text: \"gosh-ticket\", then two keys of 22 letters" );
  FACT_CHECK( ( parse_ticket( text, back ) && back.id == t.id && back.key == t.key ), "and back again, the same" );
  FACT_CHECK( ( !parse_ticket( "", back ) && !parse_ticket( text.substr( 0, 56 ), back ) && !parse_ticket( text + "x", back )
           && !parse_ticket( "gosh-tickex" + text.substr( 11 ), back )
           && !parse_ticket( text.substr( 0, 34 ) + "!" + text.substr( 35 ), back ) ), "text that is not one is refused" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Ticket );
}

FACTS_MAIN
