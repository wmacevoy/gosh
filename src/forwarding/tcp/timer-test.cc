/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/FWD-1: the round trip and the retransmission timeout (timer.h), with numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/tcp/timer.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-90s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  Gosh::RoundTrip r;
  check( "before any measurement: wait 1,000 ms", r.settled() == 1000 );
  r.sample( 100 );
  /* the first: the estimate is the sample, its variation half of it; the wait is the estimate and four variations */
  check( "one round trip of 100 ms: wait 300 ms", r.settled() == 300 );
  r.sample( 100 );
  /* variation 0.75 * 50 + 0.25 * 0 = 37.5; estimate 100: 100 + 150 */
  check( "another of 100 ms: wait 250 ms", r.settled() == 250 );
  Gosh::RoundTrip fast;
  fast.sample( 2 );
  check( "a round trip of 2 ms: never less than 200 ms", fast.settled() == 200 );
  Gosh::RoundTrip slow;
  slow.sample( 20000 );
  check( "one of 20 s: never more than 30,000 ms", slow.settled() == 30000 );
  Gosh::RoundTrip steady;
  for ( int i = 0; i < 200; i++ ) {
    steady.sample( 400 );
  }
  /* the variation goes to nothing, but is counted as 10 ms at the least */
  check( "a steady 400 ms: wait 410 ms", steady.settled() == 410 );

  /* (from the review of 2026-11-15: every sample above equals the one before it) */
  Gosh::RoundTrip v;
  v.sample( 100 ); /* estimate 100, variation 50 */
  v.sample( 300 ); /* variation 0.75 * 50 + 0.25 * |100 - 300| = 87.5; estimate 0.875 * 100 + 0.125 * 300 = 125 */
  check( "100 ms then 300 ms: wait 125 + 4 * 87.5 = 475 ms", v.settled() == 475 );
  v.sample( 60 ); /* variation 0.75 * 87.5 + 0.25 * |125 - 60| = 81.875; estimate 0.875 * 125 + 0.125 * 60 = 116.875 */
  check( "then 60 ms, less than the estimate: wait 116.875 + 4 * 81.875 = 444 ms", v.settled() == 444 );

  if ( failures ) {
    printf( "FAIL REQ1/FWD-1: %d checks of the round trip failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/FWD-1: the round trip\n" );
  return EXIT_SUCCESS;
}
