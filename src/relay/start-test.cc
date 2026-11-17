/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/PATH-1: when the client starts its relay (start.h), with numbers for time. */

#include <cstdio>
#include <cstdlib>

#include "src/relay/start.h"
#include "vendor/facts/facts.h"
/* REQ1/PATH-1: when the client starts its relay, with no clock */
FACTS( Start )
{
  using namespace Gosh;
  const uint64_t never = uint64_t( -1 );

  FACT_CHECK( ( relay_mode( "always", false ) == RELAY_NEVER && relay_mode( "auto", false ) == RELAY_NEVER ), "no command to start a relay with: never, whatever the word" );
  FACT_CHECK( ( relay_mode( "always", true ) == RELAY_ALWAYS && relay_mode( "auto", true ) == RELAY_AUTO
           && relay_mode( "", true ) == RELAY_AUTO && relay_mode( "nevermore", true ) == RELAY_AUTO ), "with a command: \"always\" is always; \"auto\", or any other word, is when needed" );
  FACT_CHECK( relay_mode( "never", true ) == RELAY_NEVER, "\"never\" is never, though there is a command" );

  FACT_CHECK( !relay_due( 8000, 5000, never, RELAY_AUTO, false, false ), "the server never heard: not at 3,000 ms after the start" );
  FACT_CHECK( relay_due( 8001, 5000, never, RELAY_AUTO, false, false ), "but at 3,001" );
  FACT_CHECK( !relay_due( 60000, 5000, 50000, RELAY_AUTO, false, false ), "the server heard once: not 10,000 ms after it was last heard" );
  FACT_CHECK( relay_due( 60001, 5000, 50000, RELAY_AUTO, false, false ), "but at 10,001" );
  FACT_CHECK( !relay_due( 8001, 5000, 5500, RELAY_AUTO, false, false ), "heard once, the start no longer counts: not at 3,001 ms after it" );
  FACT_CHECK( !relay_due( 99999, 5000, never, RELAY_AUTO, true, false ), "started already: not again" );
  FACT_CHECK( !relay_due( 99999, 5000, never, RELAY_AUTO, false, true ), "the session ending: not then" );
  FACT_CHECK( ( !relay_due( 99999, 5000, 6000, RELAY_AUTO, true, false ) && !relay_due( 99999, 5000, 6000, RELAY_AUTO, false, true )
           && relay_due( 99999, 5000, 6000, RELAY_AUTO, false, false ) ), "nor, the server once heard and long quiet, if started already or ending" );
  FACT_CHECK( ( !relay_due( 99999, 5000, never, RELAY_NEVER, false, false ) && !relay_due( 99999, 5000, never, RELAY_ALWAYS, false, false ) ), "no relay to start, or one from the start: this rule starts none" );

  FACT_CHECK( !relay_again( 5999, 5000, false ), "a relay that has gone, started 999 ms ago: not another yet" );
  FACT_CHECK( relay_again( 6000, 5000, false ), "started 1,000 ms ago: another" );
  FACT_CHECK( !relay_again( 99999, 5000, true ), "but not while the session is ending" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Start );
}

FACTS_MAIN
