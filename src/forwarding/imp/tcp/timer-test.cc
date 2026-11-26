/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/FWD-1: the round trip and the retransmission timeout (timer.h), with numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/imp/tcp/timer.h"
#include "vendor/facts/facts.h"
/* REQ1/FWD-1: the round trip */
FACTS( Timer )
{
  Gosh::Forwarding::Imp::RoundTrip r;
  FACT_CHECK( r.settled() == 1000, "before any measurement: wait 1,000 ms" );
  r.sample( 100 );
  /* the first: the estimate is the sample, its variation half of it; the wait is the estimate and four variations */
  FACT_CHECK( r.settled() == 300, "one round trip of 100 ms: wait 300 ms" );
  r.sample( 100 );
  /* variation 0.75 * 50 + 0.25 * 0 = 37.5; estimate 100: 100 + 150 */
  FACT_CHECK( r.settled() == 250, "another of 100 ms: wait 250 ms" );
  Gosh::Forwarding::Imp::RoundTrip fast;
  fast.sample( 2 );
  FACT_CHECK( fast.settled() == 200, "a round trip of 2 ms: never less than 200 ms" );
  Gosh::Forwarding::Imp::RoundTrip slow;
  slow.sample( 20000 );
  FACT_CHECK( slow.settled() == 30000, "one of 20 s: never more than 30,000 ms" );
  Gosh::Forwarding::Imp::RoundTrip steady;
  for ( int i = 0; i < 200; i++ ) {
    steady.sample( 400 );
  }
  /* the variation goes to nothing, but is counted as 10 ms at the least */
  FACT_CHECK( steady.settled() == 410, "a steady 400 ms: wait 410 ms" );

  /* (from the review of 2026-11-15: every sample above equals the one before it) */
  Gosh::Forwarding::Imp::RoundTrip v;
  v.sample( 100 ); /* estimate 100, variation 50 */
  v.sample( 300 ); /* variation 0.75 * 50 + 0.25 * |100 - 300| = 87.5; estimate 0.875 * 100 + 0.125 * 300 = 125 */
  FACT_CHECK( v.settled() == 475, "100 ms then 300 ms: wait 125 + 4 * 87.5 = 475 ms" );
  v.sample( 60 ); /* variation 0.75 * 87.5 + 0.25 * |125 - 60| = 81.875; estimate 0.875 * 125 + 0.125 * 60 = 116.875 */
  FACT_CHECK( v.settled() == 444, "then 60 ms, less than the estimate: wait 116.875 + 4 * 81.875 = 444 ms" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Timer );
}

FACTS_MAIN
