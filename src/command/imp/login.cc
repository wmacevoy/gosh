/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#include "src/command/imp/login.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/command/imp/options.h"
#include "src/ssh/login.h"

std::string Gosh::Command::Imp::terminal_colors( void ( *print_colors )( void ), const std::string& name )
{
  int fds[2];
  if ( pipe( fds ) < 0 ) {
    throw Failure( name + ": pipe: " + strerror( errno ) );
  }
  fflush( NULL );
  const pid_t pid = fork();
  if ( pid == 0 ) {
    close( fds[0] );
    dup2( fds[1], STDOUT_FILENO );
    print_colors();
    fflush( stdout );
    _exit( 0 );
  }
  close( fds[1] );
  std::string said;
  char buf[64];
  ssize_t n;
  while ( ( n = read( fds[0], buf, sizeof( buf ) ) ) > 0 ) {
    said.append( buf, n );
  }
  close( fds[0] );
  if ( pid > 0 ) {
    waitpid( pid, NULL, 0 );
  }
  said = strip_end( said );
  return all_digits( said ) ? said : "0";
}

void Gosh::Command::Imp::start_server( const Login& login, Startup& startup )
{
  int fds[2];
  if ( pipe( fds ) < 0 ) {
    throw Failure( login.name + ": pipe: " + strerror( errno ) );
  }
  fflush( NULL );
  const pid_t pid = fork();
  if ( pid < 0 ) {
    throw Failure( login.name + ": fork: " + strerror( errno ) );
  }
  if ( pid == 0 ) {
    close( fds[0] );
    dup2( fds[1], STDOUT_FILENO );
    dup2( fds[1], STDERR_FILENO );
    close( fds[1] );

    if ( login.local ) {
      unsetenv( "SSH_CONNECTION" );
      const char* home = getenv( "HOME" );
      if ( home && chdir( home ) < 0 ) {
        /* then from here */
      }
      const std::string line = "MOSH IP " + login.userhost + "\n";
      if ( write( STDOUT_FILENO, line.data(), line.size() ) < 0 ) {
        _exit( 1 );
      }
      execl( "/bin/sh", "sh", "-c", login.server_line.c_str(), static_cast<char*>( NULL ) );
      const std::string why = "Cannot exec " + login.server + ": " + strerror( errno ) + "\n";
      if ( write( STDOUT_FILENO, why.data(), why.size() ) < 0 ) {
        _exit( 1 );
      }
      _exit( 1 );
    }

    Gosh::Ssh::exec( login.words, login.proxy );
  }

  close( fds[1] );
  std::string pending;
  char buf[4096];
  ssize_t n;
  while ( !startup.done && ( n = read( fds[0], buf, sizeof( buf ) ) ) != 0 ) {
    if ( n < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      break;
    }
    pending.append( buf, n );
    size_t end;
    while ( !startup.done && ( end = pending.find( '\n' ) ) != std::string::npos ) {
      const std::string raw = pending.substr( 0, end );
      pending.erase( 0, end + 1 );
      if ( startup_line( startup, raw, login.pick_server, login.port_range, login.name ) ) {
        continue;
      }
      /* what ssh or the login had to say (not on our output if that is carrying data) */
      /* (gosh, REQ1/FWD-5) nor when there is no terminal: -N's standard output is not its own */
      if ( login.quiet ) {
        if ( !strip_end( raw ).empty() ) {
          fprintf( stderr, "%s\n", raw.c_str() );
        }
      } else {
        printf( "%s\n", raw.c_str() );
        fflush( stdout );
      }
    }
  }
  close( fds[0] );
  waitpid( pid, NULL, 0 );
}
