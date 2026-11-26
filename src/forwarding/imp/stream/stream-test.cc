/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/CP-7: when the client of a session that exists for a stream is finished (stream.h). */

#include <cstdio>
#include <cstdlib>

#include "src/forwarding/stream.h"
#include "vendor/facts/facts.h"
/* REQ1/CP-7: when a stream's client is finished */
FACTS( Stream )
{
  using Gosh::Forwarding::stream_over;
  const uint64_t ended = 100000;

  FACT_CHECK( !stream_over( false, 0, 0, ended + 999999, false ), "while its process is running, the stream is not over" );
  FACT_CHECK( stream_over( true, ended, 0, ended, false ), "its process has ended and nothing of the stream is left: over, at once" );
  FACT_CHECK( !stream_over( true, ended, 1, ended + 5000, false ), "its process has ended but a connection remains: not yet" );
  FACT_CHECK( stream_over( true, ended, 1, ended + 5001, false ), "five seconds after it ended, over, whatever remains" );
  FACT_CHECK( !stream_over( true, ended, 0, ended + 9999, true ), "a session already being ended is not ended again" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Stream );
}

FACTS_MAIN
