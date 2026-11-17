/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/forwarding/stream/streamchild.h"

std::vector<std::string> Network::stream_words( const std::string& joined )
{
  std::vector<std::string> words;
  std::string word;
  for ( size_t i = 0; i < joined.size(); i++ ) {
    if ( joined[i] == '\x1f' ) {
      words.push_back( word );
      word.clear();
    } else {
      word.push_back( joined[i] );
    }
  }
  words.push_back( word );
  return words;
}

/* In the new process: become the application.  Does not return. */
static void become( const Network::StreamApplication& application, bool local, int sock )
{
  /* The program we came from has its own ideas about signals. */
  sigset_t none;
  sigemptyset( &none );
  sigprocmask( SIG_SETMASK, &none, NULL );
  const int signals[] = { SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGWINCH, SIGCONT, SIGUSR1 };
  for ( size_t i = 0; i < sizeof( signals ) / sizeof( signals[0] ); i++ ) {
    signal( signals[i], SIG_DFL );
  }

  if ( !local ) {
    /* Nobody is at this end to read what we might say.  Paths are relative to home, as over ssh. */
    const int null = open( "/dev/null", O_RDWR );
    if ( null >= 0 ) {
      dup2( null, STDERR_FILENO );
    }
    const char* home = getenv( "HOME" );
    if ( home && chdir( home ) < 0 ) {
      /* then from wherever this is */
    }
  }

  const long limit = sysconf( _SC_OPEN_MAX );
  for ( int fd = 3; fd < ( limit > 0 && limit < 4096 ? limit : 4096 ); fd++ ) {
    if ( fd != sock ) {
      close( fd );
    }
  }
  _exit( application( sock ) );
}

int Network::stream_child( const StreamApplication& application, bool local, pid_t* pid )
{
  int pair[2];
  if ( socketpair( AF_UNIX, SOCK_STREAM, 0, pair ) < 0 ) {
    return -1;
  }

  const pid_t child = fork();
  if ( child < 0 ) {
    close( pair[0] );
    close( pair[1] );
    return -1;
  }
  if ( child == 0 ) {
    close( pair[0] );
    if ( !pid ) {
      /* nobody will wait for us: so be nobody's child */
      const pid_t again = fork();
      if ( again != 0 ) {
        _exit( again < 0 );
      }
    }
    become( application, local, pair[1] );
  }
  close( pair[1] );
  if ( pid ) {
    *pid = child;
  } else {
    int status;
    waitpid( child, &status, 0 );
  }
  return pair[0];
}
