/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* (The three applications were in forwarding's streamchild.cc until 2026-11-17, word for word but where they start.) */

#include "src/include/config.h"

#include "src/seam/applications.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
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
  if ( words[0] == "@connect" ) {
    return words.size() == 3 && !words[1].empty() && connect_port_valid( words[2] );
  }
  return ( words[0] == "@stdio" && words.size() == 1 ) || ( words[0] == "@sh" && words.size() == 2 );
}

bool Gosh::connect_port_valid( const std::string& port )
{
  if ( port.empty() || port.size() > 5 || port.find_first_not_of( "0123456789" ) != std::string::npos ) {
    return false;
  }
  const long n = strtol( port.c_str(), NULL, 10 );
  return n >= 1 && n <= 65535;
}

/* A TCP connection to host:port, by each address it has in turn; -1, having said why, if none answers. */
static int connect_to( const std::string& host, const std::string& port )
{
  struct addrinfo hints;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* found = NULL;
  const int error = getaddrinfo( host.c_str(), port.c_str(), &hints, &found );
  if ( error ) {
    fprintf( stderr, "gosh-server: -W %s:%s: %s\n", host.c_str(), port.c_str(), gai_strerror( error ) );
    return -1;
  }
  int fd = -1, why = 0;
  for ( struct addrinfo* a = found; a && fd < 0; a = a->ai_next ) {
    fd = socket( a->ai_family, a->ai_socktype, a->ai_protocol );
    if ( fd >= 0 && connect( fd, a->ai_addr, a->ai_addrlen ) != 0 ) {
      why = errno;
      close( fd );
      fd = -1;
    }
  }
  freeaddrinfo( found );
  if ( fd < 0 ) {
    fprintf( stderr, "gosh-server: -W %s:%s: %s\n", host.c_str(), port.c_str(), strerror( why ) );
  }
  return fd;
}

/*
 * Copies both ways between two sockets until both ways have ended. Each way ends on its own: when
 * one side has nothing more to send, the other is told so (shutdown), and what it still sends back
 * is carried on. So a target that answers after its input ends, as an ssh server does on its way
 * out, is heard to the end.
 */
static int join( int a, int b )
{
  const int from[2] = { a, b }, to[2] = { b, a };
  std::string held[2];
  bool open[2] = { true, true }, told[2] = { false, false };
  char buf[65536];
  fcntl( a, F_SETFL, fcntl( a, F_GETFL ) | O_NONBLOCK );
  fcntl( b, F_SETFL, fcntl( b, F_GETFL ) | O_NONBLOCK );

  while ( open[0] || open[1] || !held[0].empty() || !held[1].empty() ) {
    struct pollfd fds[4];
    for ( int w = 0; w < 2; w++ ) {
      fds[2 * w] = { open[w] && held[w].size() < sizeof( buf ) ? from[w] : -1, POLLIN, 0 };
      fds[2 * w + 1] = { held[w].empty() ? -1 : to[w], POLLOUT, 0 };
    }
    if ( poll( fds, 4, -1 ) < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      return 1;
    }
    for ( int w = 0; w < 2; w++ ) {
      if ( fds[2 * w].revents ) {
        const ssize_t n = read( from[w], buf, sizeof( buf ) );
        if ( n > 0 ) {
          held[w].append( buf, n );
        } else if ( n == 0 || ( errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) ) {
          open[w] = false;
        }
      }
      if ( fds[2 * w + 1].revents ) {
        const ssize_t n = write( to[w], held[w].data(), held[w].size() );
        if ( n > 0 ) {
          held[w].erase( 0, n );
        } else if ( n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) {
          return 1; /* nobody to write to any more */
        }
      }
      if ( !open[w] && held[w].empty() && !told[w] ) {
        shutdown( to[w], SHUT_WR ); /* this way has ended: the other side is told */
        told[w] = true;
      }
    }
  }
  return 0;
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
  if ( words[0] == "@connect" ) {
    const std::string host = words[1], port = words[2];
    return [host, port]( int sock ) {
      const int target = connect_to( host, port );
      return target < 0 ? 1 : join( sock, target );
    };
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
