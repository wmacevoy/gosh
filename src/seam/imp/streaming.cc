/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/seam/imp/session.h"

#include <cerrno>
#include <cstdio>

#include <sys/wait.h>

#include "src/forwarding/stream.h"
#include "src/forwarding/streamchild.h"
#include "src/network/network.h"
#include "src/seam/asked.h"
#include "src/seam/imp/applications.h"

using namespace Gosh::Seam;
using namespace Gosh::Seam::Imp;

/* Has the process at our end of the stream finished? Then, soon, so have we (forwarding/stream.h says when). */
void Gosh::Seam::Imp::tend_stream( void )
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
       && Gosh::Forwarding::stream_over( s.stream.pid == -1, s.stream.ended, s.forwarding->stream_connections(), Network::timestamp(),
                       s.ending() ) ) {
    s.end_it();
  }
}

void Gosh::Seam::Imp::take_stream_words( void )
{
  Session& s = session();
  /* (only when there is a stream: the words of an empty string are one empty word, which is not "no stream") */
  s.stream.words = s.options.stream.empty() ? std::vector<std::string>() : Gosh::Forwarding::stream_words( s.options.stream );
}

void Gosh::Seam::Imp::serve_stream( void )
{
  Session& s = session();
  const std::vector<std::string> words = s.stream.words;
  s.forwarding->set_stream_spawner(
    [words]() { return stream_words_valid( words ) ? Gosh::Forwarding::stream_child( stream_application( words, false ), false, NULL ) : -1; } );
}

void Gosh::Seam::Imp::stream_never_ran( void )
{
  Session& s = session();
  s.stream.pid = -1;
  s.stream.ended = Network::timestamp();
}

void Gosh::Seam::Imp::start_stream( void )
{
  Session& s = session();
  pid_t pid = 0;
  const int fd = stream_words_valid( s.stream.words ) ? Gosh::Forwarding::stream_child( stream_application( s.stream.words, true ), true, &pid ) : -1;
  if ( fd < 0 ) {
    stream_never_ran();
    return;
  }
  s.stream.pid = pid;
  s.forwarding->open_stream( fd ); /* (if it can't be, the process sees its stream end at once) */
}

int Gosh::Seam::stream_exit( bool success )
{
  Imp::Session& s = Imp::session();
  const int status = ( s.loop_returned && s.stream.pid == -1 ) ? s.stream.status : 1;
  if ( status == 1 && !success ) {
    fputs( "gosh: the session ended before its work was done.\n", stderr );
  }
  return status;
}
