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

namespace {

void exec_words( const std::vector<std::string>& words )
{
  std::vector<char*> argv;
  for ( size_t i = 0; i < words.size(); i++ ) {
    argv.push_back( const_cast<char*>( words[i].c_str() ) );
  }
  argv.push_back( NULL );
  execvp( argv[0], argv.data() );
}

}

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

std::string Gosh::Command::Imp::ssh_config_text( const std::vector<std::string>& ssh, const std::string& host )
{
  int out[2];
  if ( ssh.empty() || pipe( out ) < 0 ) {
    return std::string();
  }
  std::vector<std::string> words = ssh;
  words.push_back( "-G" );
  words.push_back( host );
  const pid_t child = fork();
  if ( child < 0 ) {
    close( out[0] );
    close( out[1] );
    return std::string();
  }
  if ( child == 0 ) {
    dup2( out[1], STDOUT_FILENO );
    const int null = open( "/dev/null", O_RDWR );
    if ( null >= 0 ) {
      dup2( null, STDIN_FILENO );
      dup2( null, STDERR_FILENO );
    }
    close( out[0] );
    std::vector<char*> argv;
    for ( size_t i = 0; i < words.size(); i++ ) {
      argv.push_back( const_cast<char*>( words[i].c_str() ) );
    }
    argv.push_back( NULL );
    execvp( argv[0], &argv[0] );
    _exit( 127 );
  }
  close( out[1] );
  std::string text;
  char buf[4096];
  ssize_t n;
  while ( ( n = read( out[0], buf, sizeof( buf ) ) ) != 0 ) {
    if ( n < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      break;
    }
    text.append( buf, n );
  }
  close( out[0] );
  int status = 0;
  while ( waitpid( child, &status, 0 ) < 0 && errno == EINTR ) {
  }
  return WIFEXITED( status ) && WEXITSTATUS( status ) == 0 ? text : std::string();
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

    if ( login.proxy ) {
      /* Unusual shells, and what they are told to do at start, break a ProxyCommand mysteriously. */
      setenv( "SHELL", "/bin/sh", 1 );
    }
    exec_words( login.words );
    const std::string why = "Cannot exec ssh: " + std::string( strerror( errno ) ) + "\n";
    if ( write( STDOUT_FILENO, why.data(), why.size() ) < 0 ) {
      _exit( 1 );
    }
    _exit( 1 );
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
