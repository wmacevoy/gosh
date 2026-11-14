/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/* REQ1/CP-7: when the client of a session that exists for a stream is finished (stream.h). */

#include <cstdio>
#include <cstdlib>

#include "src/forwarding/stream.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-76s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  using Gosh::stream_over;
  const uint64_t ended = 100000;

  check( "while its process is running, the stream is not over", !stream_over( false, 0, 0, ended + 999999, false ) );
  check( "its process has ended and nothing of the stream is left: over, at once", stream_over( true, ended, 0, ended, false ) );
  check( "its process has ended but a connection remains: not yet", !stream_over( true, ended, 1, ended + 5000, false ) );
  check( "five seconds after it ended, over, whatever remains", stream_over( true, ended, 1, ended + 5001, false ) );
  check( "a session already being ended is not ended again", !stream_over( true, ended, 0, ended + 9999, true ) );

  if ( failures ) {
    printf( "FAIL REQ1/CP-7: %d checks failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/CP-7: when a stream's client is finished\n" );
  return EXIT_SUCCESS;
}
