/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* (The three applications were in forwarding's streamchild.cc until 2026-11-17, word for word but where they start.) */

#include "src/include/config.h"

#include "src/seam/applications.h"

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/copy/filecopy.h"

bool Gosh::stream_words_valid( const std::vector<std::string>& words )
{
  if ( words.empty() ) {
    return false;
  }
  if ( words[0] == "@copy" ) {
    FileCopy::Options options;
    return FileCopy::parse( std::vector<std::string>( words.begin() + 1, words.end() ), options );
  }
  return ( words[0] == "@stdio" && words.size() == 1 ) || ( words[0] == "@sh" && words.size() == 2 );
}

/* Copies between our own standard input and output and the socket, until the other end has finished. */
static int stdio_pump( int sock )
{
  std::string to_sock, to_out;
  bool in_open = true, in_shut = false, sock_open = true;
  char buf[65536];
  fcntl( sock, F_SETFL, fcntl( sock, F_GETFL ) | O_NONBLOCK );

  while ( sock_open || !to_out.empty() ) {
    const bool read_in = in_open && to_sock.size() < sizeof( buf );
    const bool read_sock = sock_open && to_out.size() < sizeof( buf );
    struct pollfd fds[3] = {
      { read_in ? STDIN_FILENO : -1, POLLIN, 0 },
      { ( read_sock || !to_sock.empty() ) ? sock : -1,
        static_cast<short>( ( read_sock ? POLLIN : 0 ) | ( to_sock.empty() ? 0 : POLLOUT ) ), 0 },
      { to_out.empty() ? -1 : STDOUT_FILENO, POLLOUT, 0 }
    };
    if ( poll( fds, 3, -1 ) < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      return 1;
    }
    if ( fds[0].revents ) {
      ssize_t n = read( STDIN_FILENO, buf, sizeof( buf ) );
      if ( n > 0 ) {
        to_sock.append( buf, n );
      } else if ( n == 0 || ( errno != EINTR && errno != EAGAIN ) ) {
        in_open = false;
      }
    }
    if ( read_sock && ( fds[1].revents & ( POLLIN | POLLHUP | POLLERR ) ) ) {
      ssize_t n = read( sock, buf, sizeof( buf ) );
      if ( n > 0 ) {
        to_out.append( buf, n );
      } else if ( n == 0 || ( errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) ) {
        sock_open = false;
      }
    }
    if ( !to_sock.empty() && ( fds[1].revents & POLLOUT ) ) {
      ssize_t n = write( sock, to_sock.data(), to_sock.size() );
      if ( n > 0 ) {
        to_sock.erase( 0, n );
      } else if ( n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) {
        return 1; /* nobody at the other end any more */
      }
    }
    if ( !in_open && !in_shut && to_sock.empty() ) {
      shutdown( sock, SHUT_WR ); /* our input has ended, and so the other end's */
      in_shut = true;
    }
    if ( fds[2].revents ) {
      ssize_t n = write( STDOUT_FILENO, to_out.data(), to_out.size() );
      if ( n > 0 ) {
        to_out.erase( 0, n );
      } else if ( n < 0 && errno != EINTR && errno != EAGAIN ) {
        return 1;
      }
    }
  }
  return 0;
}

/* The stream as standard input and output, and the socket itself closed: the other end sees the stream end when they do. */
static void stream_as_stdio( int sock )
{
  dup2( sock, STDIN_FILENO );
  dup2( sock, STDOUT_FILENO );
  if ( sock > STDERR_FILENO ) {
    close( sock );
  }
}

Network::StreamApplication Gosh::stream_application( const std::vector<std::string>& words, bool local )
{
  if ( words[0] == "@copy" ) {
    return [words, local]( int sock ) {
      FileCopy::Options options;
      if ( !FileCopy::parse( std::vector<std::string>( words.begin() + 1, words.end() ), options ) ) {
        return 2;
      }
      options.local = local;
      stream_as_stdio( sock );
      return FileCopy::run( options, STDIN_FILENO, STDOUT_FILENO );
    };
  }
  if ( words[0] == "@stdio" ) {
    return []( int sock ) { return stdio_pump( sock ); };
  }
  const std::string command = words[1];
  return [command]( int sock ) {
    /* a command, for the user's shell */
    stream_as_stdio( sock );
    const char* shell = getenv( "SHELL" );
    if ( !shell || !*shell ) {
      const struct passwd* pw = getpwuid( getuid() );
      shell = ( pw && pw->pw_shell && *pw->pw_shell ) ? pw->pw_shell : "/bin/sh";
    }
    execl( shell, shell, "-c", command.c_str(), static_cast<char*>( NULL ) );
    return 127;
  };
}
