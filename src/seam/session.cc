/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

#include "src/seam/hooks.h"

#include <sys/wait.h>

#include "src/forwarding/stream.h"
#include "src/network/network.h"

/*
 * The one process-wide value the seam has (design v2, 4.2). It rests on "one mosh session per
 * process", which is true of both programs. It is made on first use, so its order among other
 * statics does not matter.
 */
Gosh::Session& Gosh::session( void )
{
  static Session the_session;
  return the_session;
}

/* Has the process at our end of the stream finished? Then, soon, so have we (forwarding/stream.h says when). */
void Gosh::tend_stream( void )
{
  Session& s = session();
  if ( s.stream_pid > 0 ) {
    int status = 0;
    if ( waitpid( s.stream_pid, &status, WNOHANG ) == s.stream_pid ) {
      s.stream_status = WIFEXITED( status ) ? WEXITSTATUS( status ) : 128 + WTERMSIG( status );
      s.stream_pid = -1;
      s.stream_ended = Network::timestamp();
    }
  }
  if ( s.forwarding && s.ending && s.end_it
       && stream_over( s.stream_pid == -1, s.stream_ended, s.forwarding->stream_connections(), Network::timestamp(),
                       s.ending() ) ) {
    s.end_it();
  }
}
