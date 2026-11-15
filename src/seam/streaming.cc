/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/seam/session.h"

#include <cerrno>

#include <sys/wait.h>

#include "src/forwarding/stream/stream.h"
#include "src/network/network.h"

using namespace Gosh;

/* Has the process at our end of the stream finished? Then, soon, so have we (forwarding/stream.h says when). */
void Gosh::tend_stream( void )
{
  Session& s = session();
  if ( s.stream.pid > 0 ) {
    int status = 0;
    const pid_t waited = waitpid( s.stream.pid, &status, WNOHANG );
    if ( waited == s.stream.pid ) {
      s.stream.status = WIFEXITED( status ) ? WEXITSTATUS( status ) : 128 + WTERMSIG( status );
      s.stream.pid = -1;
      s.stream.ended = Network::timestamp();
    } else if ( waited < 0 && errno == ECHILD ) {
      /*
       * There is no such child to wait for: whoever started us had child signals ignored, so the
       * system has taken it away already, and with it how it ended. It has ended; without this
       * we would wait for ever. (The fork does wait for ever.) Its status is not known: 1.
       */
      s.stream.status = 1;
      s.stream.pid = -1;
      s.stream.ended = Network::timestamp();
    }
  }
  if ( s.forwarding && s.ending && s.end_it
       && stream_over( s.stream.pid == -1, s.stream.ended, s.forwarding->stream_connections(), Network::timestamp(),
                       s.ending() ) ) {
    s.end_it();
  }
}
