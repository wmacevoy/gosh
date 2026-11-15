/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/PATH-1: when the client starts its relay (start.h), with numbers for time. */

#include <cstdio>
#include <cstdlib>

#include "src/relay/start.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-82s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  using namespace Gosh;
  const uint64_t never = uint64_t( -1 );

  check( "no command to start a relay with: never, whatever the word",
         relay_mode( "always", false ) == RELAY_NEVER && relay_mode( "auto", false ) == RELAY_NEVER );
  check( "with a command: \"always\" is always; \"auto\", or any other word, is when needed",
         relay_mode( "always", true ) == RELAY_ALWAYS && relay_mode( "auto", true ) == RELAY_AUTO
           && relay_mode( "", true ) == RELAY_AUTO && relay_mode( "nevermore", true ) == RELAY_AUTO );
  check( "\"never\" is never, though there is a command", relay_mode( "never", true ) == RELAY_NEVER );

  check( "the server never heard: not at 3,000 ms after the start", !relay_due( 8000, 5000, never, RELAY_AUTO, false, false ) );
  check( "but at 3,001", relay_due( 8001, 5000, never, RELAY_AUTO, false, false ) );
  check( "the server heard once: not 10,000 ms after it was last heard", !relay_due( 60000, 5000, 50000, RELAY_AUTO, false, false ) );
  check( "but at 10,001", relay_due( 60001, 5000, 50000, RELAY_AUTO, false, false ) );
  check( "heard once, the start no longer counts: not at 3,001 ms after it", !relay_due( 8001, 5000, 5500, RELAY_AUTO, false, false ) );
  check( "started already: not again", !relay_due( 99999, 5000, never, RELAY_AUTO, true, false ) );
  check( "the session ending: not then", !relay_due( 99999, 5000, never, RELAY_AUTO, false, true ) );
  check( "nor, the server once heard and long quiet, if started already or ending",
         !relay_due( 99999, 5000, 6000, RELAY_AUTO, true, false ) && !relay_due( 99999, 5000, 6000, RELAY_AUTO, false, true )
           && relay_due( 99999, 5000, 6000, RELAY_AUTO, false, false ) );
  check( "no relay to start, or one from the start: this rule starts none",
         !relay_due( 99999, 5000, never, RELAY_NEVER, false, false ) && !relay_due( 99999, 5000, never, RELAY_ALWAYS, false, false ) );

  if ( failures ) {
    printf( "FAIL REQ1/PATH-1: %d checks of when the relay starts failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/PATH-1: when the client starts its relay, with no clock\n" );
  return EXIT_SUCCESS;
}
