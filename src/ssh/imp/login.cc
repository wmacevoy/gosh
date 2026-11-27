/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#include "src/ssh/login.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

Gosh::Ssh::Words Gosh::Ssh::login_words( const Words& ssh, bool pty, const std::string& remote_ip, const std::string& family,
                                          const std::string& proxy, const std::string& userhost, const std::string& line )
{
  Words words = ssh;
  words.push_back( "-n" );
  if ( pty ) {
    words.push_back( "-tt" );
  }
  std::string ask_address;
  if ( remote_ip == "remote" ) {
    /* Ask the server for its address.  The login shell may not be a Bourne shell, so say which. */
    ask_address = "sh -c "
                  + shell_quote( Words(
                    1, "[ -n \"$SSH_CONNECTION\" ] && printf \"\\nMOSH SSH_CONNECTION %s\\n\" \"$SSH_CONNECTION\"" ) )
                  + " ; ";
    if ( family == "inet" ) {
      words.push_back( "-4" );
    } else if ( family == "inet6" ) {
      words.push_back( "-6" );
    }
  } else if ( remote_ip == "proxy" ) {
    words.push_back( "-S" );
    words.push_back( "none" );
    words.push_back( "-o" );
    words.push_back( "ProxyCommand=" + proxy );
  }
  words.push_back( userhost );
  words.push_back( "--" );
  words.push_back( ask_address + line );
  return words;
}

Gosh::Ssh::Words Gosh::Ssh::asking_nothing( const Words& ssh, const std::string& userhost, const std::string& remote )
{
  Words words = ssh;
  words.push_back( "-T" );
  words.push_back( "-o" );
  words.push_back( "BatchMode=yes" );
  words.push_back( userhost );
  words.push_back( "--" );
  words.push_back( remote );
  return words;
}

std::string Gosh::Ssh::config_text( const Words& ssh, const std::string& host )
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

void Gosh::Ssh::exec( const Words& words, bool proxy )
{
  if ( proxy ) {
    setenv( "SHELL", "/bin/sh", 1 );
  }
  std::vector<char*> argv;
  for ( size_t i = 0; i < words.size(); i++ ) {
    argv.push_back( const_cast<char*>( words[i].c_str() ) );
  }
  argv.push_back( NULL );
  execvp( argv[0], argv.data() );
  const std::string why = "Cannot exec ssh: " + std::string( strerror( errno ) ) + "\n";
  if ( write( STDOUT_FILENO, why.data(), why.size() ) < 0 ) {
    _exit( 1 );
  }
  _exit( 1 );
}
