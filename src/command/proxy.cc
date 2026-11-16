/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#include "src/include/config.h"

#include "src/command/proxy.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

#include "src/command/moshwrapper.h"
#include "src/command/options.h"
#include "src/command/words.h"

std::vector<Gosh::Command::Address> Gosh::Command::resolve( const std::string& host, const std::string& port, const std::string& family,
                                          const std::string& name )
{
  int af = AF_UNSPEC;
  if ( family == "inet" ) {
    af = AF_INET;
  } else if ( family == "inet6" ) {
    af = AF_INET6;
  }

  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = af;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_NUMERICHOST;
  int err = getaddrinfo( host.c_str(), port.c_str(), &hints, &res );
  if ( err ) {
    hints.ai_flags = 0;
    err = getaddrinfo( host.c_str(), port.c_str(), &hints, &res );
    if ( err ) {
      throw Failure( name + ": could not resolve " + host + ": " + gai_strerror( err ) );
    }
  }

  std::vector<Address> found;
  for ( struct addrinfo* ai = res; ai; ai = ai->ai_next ) {
    char text[NI_MAXHOST];
    if ( getnameinfo( ai->ai_addr, ai->ai_addrlen, text, sizeof( text ), NULL, 0, NI_NUMERICHOST ) != 0 ) {
      continue;
    }
    Address a;
    a.family = ai->ai_family;
    memcpy( &a.addr, ai->ai_addr, ai->ai_addrlen );
    a.len = ai->ai_addrlen;
    a.text = text;
    found.push_back( a );
  }
  freeaddrinfo( res );

  if ( family.compare( 0, 7, "prefer-" ) == 0 ) {
    const int preferred = family == "prefer-inet6" ? AF_INET6 : AF_INET;
    std::vector<Address> ordered;
    for ( int pass = 0; pass < 2; pass++ ) {
      for ( size_t i = 0; i < found.size(); i++ ) {
        if ( ( found[i].family == preferred ) == ( pass == 0 ) ) {
          ordered.push_back( found[i] );
        }
      }
    }
    found = ordered;
  } else if ( af == AF_UNSPEC && family != "all" ) {
    /* "auto": for hosts with addresses of one kind only */
    for ( size_t i = 1; i < found.size(); i++ ) {
      if ( found[i].family != found[0].family ) {
        throw Failure( name + ": host has both IPv4 and IPv6 addresses, use --family to specify behavior" );
      }
    }
  }
  return found;
}

std::string Gosh::Command::self_path( const std::string& argv0 )
{
  char buf[4096];
#ifdef __APPLE__
  uint32_t size = sizeof( buf );
  if ( _NSGetExecutablePath( buf, &size ) == 0 ) {
    return buf;
  }
#else
  const ssize_t n = readlink( "/proc/self/exe", buf, sizeof( buf ) - 1 );
  if ( n > 0 ) {
    buf[n] = '\0';
    return buf;
  }
#endif
  if ( argv0.find( '/' ) != std::string::npos ) {
    if ( argv0[0] == '/' ) {
      return argv0;
    }
    if ( getcwd( buf, sizeof( buf ) ) ) {
      return std::string( buf ) + "/" + argv0;
    }
    return argv0;
  }
  const char* path = getenv( "PATH" );
  const std::vector<std::string> dirs = split( path ? path : "", ':' );
  for ( size_t i = 0; i < dirs.size(); i++ ) {
    const std::string candidate = ( dirs[i].empty() ? "." : dirs[i] ) + "/" + argv0;
    if ( access( candidate.c_str(), X_OK ) == 0 ) {
      return candidate;
    }
  }
  return argv0;
}

namespace {
/* ssh reads %h, %p and the like in a ProxyCommand: a % of the path's own is written %% (the review of item 10) */
std::string percents_doubled( const std::string& s )
{
  std::string out;
  for ( size_t i = 0; i < s.size(); i++ ) {
    out += s[i] == '%' ? "%%" : std::string( 1, s[i] );
  }
  return out;
}
}

std::string Gosh::Command::proxy_command( const std::string& self, const std::string& family )
{
#ifdef __CYGWIN__
  /* ssh may be Windows's own, which runs this with cmd.exe: a Windows path, in double quotes */
  char windows[4096];
  std::string path = self;
  if ( cygwin_conv_path( CCP_POSIX_TO_WIN_A | CCP_ABSOLUTE, self.c_str(), windows, sizeof( windows ) ) == 0 ) {
    path = windows;
  }
  return "\"" + percents_doubled( path ) + "\" --mosh-fake-proxy " + family + " %h %p";
#else
  return percents_doubled( shell_quote( std::vector<std::string>( 1, self ) ) ) + " --mosh-fake-proxy " + family + " %h %p";
#endif
}

int MoshWrapper::fake_proxy( const std::string& family, const std::string& host, const std::string& port )
{
  std::vector<Gosh::Command::Address> addresses;
  try {
    addresses = Gosh::Command::resolve( host, port, family, "gosh" ); /* (run as gosh-client: the command's name is not known here) */
  } catch ( const Gosh::Command::Failure& f ) {
    fprintf( stderr, "%s\n", f.message.c_str() );
    return 1;
  }

  int sock = -1;
  std::string tried, why = "no addresses";
  for ( size_t i = 0; i < addresses.size() && sock < 0; i++ ) {
    tried = addresses[i].text;
    sock = socket( addresses[i].family, SOCK_STREAM, IPPROTO_TCP );
    if ( sock >= 0
         && connect( sock, reinterpret_cast<const struct sockaddr*>( &addresses[i].addr ), addresses[i].len ) < 0 ) {
      why = strerror( errno );
      close( sock );
      sock = -1;
    }
  }
  if ( sock < 0 ) {
    fprintf( stderr, "gosh: Could not connect to %s, last tried %s: %s\n", host.c_str(), tried.c_str(), why.c_str() );
    return 1;
  }
  fprintf( stderr, "MOSH IP %s\n", tried.c_str() );
  fflush( stderr );

  /* Act like netcat */
  signal( SIGHUP, SIG_IGN );
  signal( SIGPIPE, SIG_IGN );
  std::string to_sock, to_out;
  bool in_open = true, in_shut = false, sock_open = true;
  char buf[16384];
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
      const ssize_t n = read( STDIN_FILENO, buf, sizeof( buf ) );
      if ( n > 0 ) {
        to_sock.append( buf, n );
      } else if ( n == 0 || errno != EINTR ) {
        in_open = false;
      }
    }
    if ( read_sock && ( fds[1].revents & ( POLLIN | POLLHUP | POLLERR ) ) ) {
      const ssize_t n = recv( sock, buf, sizeof( buf ), 0 );
      if ( n > 0 ) {
        to_out.append( buf, n );
      } else if ( n == 0 || errno != EINTR ) {
        sock_open = false;
      }
    }
    if ( !to_sock.empty() && ( fds[1].revents & POLLOUT ) ) {
      const ssize_t n = send( sock, to_sock.data(), to_sock.size(), 0 );
      if ( n > 0 ) {
        to_sock.erase( 0, n );
      } else if ( n < 0 && errno != EINTR ) {
        return 1;
      }
    }
    if ( !in_open && !in_shut && to_sock.empty() ) {
      shutdown( sock, SHUT_WR );
      in_shut = true;
    }
    if ( fds[2].revents ) {
      const ssize_t n = write( STDOUT_FILENO, to_out.data(), to_out.size() );
      if ( n > 0 ) {
        to_out.erase( 0, n );
      } else if ( n < 0 && errno != EINTR ) {
        return 1;
      }
    }
  }
  return 0;
}
