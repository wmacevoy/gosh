/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The edge of path-finding (punch.h). The fork's text (mosh-1.4.0-pf.7, src/network/punch.cc), with what it decided moved to finding.h. */

#include "src/include/config.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/crypto/prng.h"
#include "src/pathfinding/punch.h"

using namespace Network;
using namespace Gosh::Pathfinding;
using Gosh::Pathfinding::Imp::PathFinding;

/*
 * (gosh, REQ17/PATH-8) The socket takes both families where the system lets one socket do so: an
 * IPv4 address is then written in IPv6's form for it (::ffff:a.b.c.d). The logic sees addresses
 * as they are, IPv4 as IPv4; these two put them in the socket's form and back.
 */
static Addr from_socket( const Addr& a )
{
  if ( a.sa.sa_family != AF_INET6 ) {
    return a;
  }
  const unsigned char* b = a.sin6.sin6_addr.s6_addr;
  static const unsigned char mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
  if ( memcmp( b, mapped, 12 ) != 0 ) {
    return a;
  }
  Addr v4;
  memset( &v4, 0, sizeof( v4 ) );
  v4.sin.sin_family = AF_INET;
  v4.sin.sin_port = a.sin6.sin6_port;
  memcpy( &v4.sin.sin_addr.s_addr, b + 12, 4 );
  return v4;
}

static socklen_t to_socket( int family, const Addr& a, Addr& out )
{
  if ( family == AF_INET6 && a.sa.sa_family == AF_INET ) {
    memset( &out, 0, sizeof( out ) );
    out.sin6.sin6_family = AF_INET6;
    out.sin6.sin6_port = a.sin.sin_port;
    out.sin6.sin6_addr.s6_addr[10] = 0xff;
    out.sin6.sin6_addr.s6_addr[11] = 0xff;
    memcpy( out.sin6.sin6_addr.s6_addr + 12, &a.sin.sin_addr.s_addr, 4 );
    return sizeof( out.sin6 );
  }
  out = a;
  return a.sa.sa_family == AF_INET6 ? sizeof( a.sin6 ) : sizeof( a.sin );
}

/* Bound to a port from low to high, the first free; or, with none free or no range (0), to any. */
static bool bind_in( int fd, struct sockaddr* any, socklen_t len, int low, int high )
{
  for ( int port = low; low > 0 && port <= high; port++ ) {
    const uint16_t p = htons( static_cast<uint16_t>( port ) );
    if ( any->sa_family == AF_INET6 ) {
      reinterpret_cast<struct sockaddr_in6*>( any )->sin6_port = p;
    } else {
      reinterpret_cast<struct sockaddr_in*>( any )->sin_port = p;
    }
    if ( bind( fd, any, len ) == 0 ) {
      return true;
    }
  }
  if ( any->sa_family == AF_INET6 ) {
    reinterpret_cast<struct sockaddr_in6*>( any )->sin6_port = 0;
  } else {
    reinterpret_cast<struct sockaddr_in*>( any )->sin_port = 0;
  }
  return bind( fd, any, len ) == 0;
}

/* A socket for both families if there can be one, or else for IPv4; and which it is. */
static int both_families( int& family, int low, int high )
{
  family = AF_INET6;
  int fd = socket( AF_INET6, SOCK_DGRAM, 0 );
  const int only = 0;
  if ( fd >= 0 && setsockopt( fd, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof( only ) ) == 0 ) {
    struct sockaddr_in6 any;
    memset( &any, 0, sizeof( any ) );
    any.sin6_family = AF_INET6;
    if ( bind_in( fd, reinterpret_cast<struct sockaddr*>( &any ), sizeof( any ), low, high ) ) {
      return fd;
    }
  }
  if ( fd >= 0 ) {
    close( fd );
  }
  family = AF_INET;
  fd = socket( AF_INET, SOCK_DGRAM, 0 );
  struct sockaddr_in any;
  memset( &any, 0, sizeof( any ) );
  any.sin_family = AF_INET;
  if ( fd >= 0 && !bind_in( fd, reinterpret_cast<struct sockaddr*>( &any ), sizeof( any ), low, high ) ) {
    close( fd );
    fd = -1;
  }
  return fd;
}

/*
 * Ask the servers where datagrams from this socket appear to come from.
 * Looking their names up can take a while, and we have datagrams to
 * carry meanwhile, so another process does the asking; the answers come
 * to the socket, which is still ours to read.
 */
static void ask_stun( int sock, int family, const std::vector<std::string>& servers )
{
  if ( servers.empty() ) {
    return;
  }
  char txid[12];
  PRNG().fill( txid, sizeof( txid ) );
  const std::string request = Gosh::Stun::stun_request( txid );

  const pid_t pid = fork();
  if ( pid < 0 ) {
    return;
  }
  if ( pid > 0 ) {
    int status;
    waitpid( pid, &status, 0 ); /* (it leaves at once: see below) */
    return;
  }
  if ( fork() != 0 ) {
    _exit( 0 ); /* nobody need wait for the one that does the work */
  }
  signal( SIGALRM, SIG_DFL );
  alarm( 10 );
  for ( int round = 0; round < 2; round++ ) { /* (twice: a datagram can be lost) */
    for ( size_t i = 0; i < servers.size(); i++ ) {
      const size_t colon = servers[i].rfind( ':' );
      if ( colon == std::string::npos ) {
        continue;
      }
      std::string host = servers[i].substr( 0, colon );
      if ( host.size() > 2 && host[0] == '[' && host[host.size() - 1] == ']' ) {
        host = host.substr( 1, host.size() - 2 );
      }
      struct addrinfo hints, *res = NULL;
      memset( &hints, 0, sizeof( hints ) );
      hints.ai_family = family == AF_INET6 ? AF_UNSPEC : AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      if ( getaddrinfo( host.c_str(), servers[i].substr( colon + 1 ).c_str(), &hints, &res ) != 0 ) {
        continue;
      }
      /* (each address the name has, of each family: whichever we can be seen from) */
      for ( struct addrinfo* a = res; a; a = a->ai_next ) {
        Addr to, sent;
        memset( &to, 0, sizeof( to ) );
        memcpy( &to, a->ai_addr, a->ai_addrlen < sizeof( to ) ? a->ai_addrlen : sizeof( to ) );
        const socklen_t len = to_socket( family, to, sent );
        sendto( sock, request.data(), request.size(), 0, &sent.sa, len );
      }
      freeaddrinfo( res );
    }
    usleep( 400000 );
  }
  _exit( 0 );
}

Punch::Punch( bool s_initiator,
              const std::string& key,
              const std::vector<std::string>& ask,
              uint64_t now,
              int port_low,
              int port_high )
  : sock( -1 ), family( AF_INET ), finding( s_initiator, key, now )
{
  sock = both_families( family, port_low, port_high );
  if ( sock < 0 ) {
    throw NetworkException( "punch: socket", errno );
  }
  fcntl( sock, F_SETFL, fcntl( sock, F_GETFL ) | O_NONBLOCK );
  fcntl( sock, F_SETFD, FD_CLOEXEC );

  /*
   * Where we are on our own network, too.  If the other end is on the
   * same one (two machines in a room, behind one NAT), that is the way
   * to it: the outside address of a NAT is seldom any use from inside.
   */
  struct ifaddrs* interfaces = NULL;
  if ( getifaddrs( &interfaces ) == 0 ) {
    for ( struct ifaddrs* i = interfaces; i; i = i->ifa_next ) {
      if ( !i->ifa_addr || !( i->ifa_flags & IFF_UP ) || ( i->ifa_flags & IFF_LOOPBACK ) ) {
        continue;
      }
      Addr mine;
      memset( &mine, 0, sizeof( mine ) );
      if ( i->ifa_addr->sa_family == AF_INET ) {
        memcpy( &mine.sin, i->ifa_addr, sizeof( mine.sin ) );
        mine.sin.sin_port = htons( local_port() );
      } else if ( i->ifa_addr->sa_family == AF_INET6 && family == AF_INET6 ) {
        memcpy( &mine.sin6, i->ifa_addr, sizeof( mine.sin6 ) );
        /* (an address of this link alone needs its interface named to be used: not one to hand out) */
        if ( IN6_IS_ADDR_LINKLOCAL( &mine.sin6.sin6_addr ) ) {
          continue;
        }
        mine.sin6.sin6_port = htons( local_port() );
        mine.sin6.sin6_scope_id = 0;
      } else {
        continue;
      }
      finding.add_own_address( Gosh::Stun::format_ip_port( mine ) );
    }
    freeifaddrs( interfaces );
  }

  ask_stun( sock, family, ask );
}

Punch::~Punch()
{
  close( sock );
}

uint16_t Punch::local_port( void ) const
{
  Addr mine;
  socklen_t len = sizeof( mine );
  if ( getsockname( sock, &mine.sa, &len ) < 0 ) {
    return 0;
  }
  return ntohs( mine.sa.sa_family == AF_INET6 ? mine.sin6.sin6_port : mine.sin.sin_port );
}

/* Sends what the logic says to send. */
void Punch::flush( void )
{
  const std::vector<PathFinding::Outgoing> out = finding.take_outbox();
  for ( size_t i = 0; i < out.size(); i++ ) {
    Addr to;
    const socklen_t len = to_socket( family, out[i].to, to );
    if ( family == AF_INET && to.sa.sa_family != AF_INET ) {
      continue; /* (an IPv6 address, and a socket that cannot reach one) */
    }
    sendto( sock, out[i].bytes.data(), out[i].bytes.size(), 0, &to.sa, len );
  }
}

bool Punch::use_meetup( const std::string& ip_port, const Gosh::Meetup::Ticket& t )
{
  struct timeval tv;
  gettimeofday( &tv, NULL );
  try {
    return finding.use_meetup( ip_port, t, static_cast<uint64_t>( tv.tv_sec ) * 1000 + tv.tv_usec / 1000 );
  } catch ( const Crypto::CryptoException& ) {
    return false;
  }
}

void Punch::tick( uint64_t now )
{
  finding.tick( now );
  flush();
}

Punch::Got Punch::receive( uint64_t now, std::string& datagram )
{
  char whole[65536];
  Addr from;
  socklen_t from_len = sizeof( from );
  const ssize_t n = recvfrom( sock, whole, sizeof( whole ), 0, &from.sa, &from_len );
  if ( n < 0 ) {
    return ( errno == EAGAIN || errno == EWOULDBLOCK ) ? NOTHING : OTHER;
  }
  const Got got = finding.receive( now, whole, n, from_socket( from ), datagram );
  flush();
  return got;
}

void Punch::send( const std::string& datagram )
{
  finding.send( datagram );
  flush();
}

void Punch::goodbye( void )
{
  finding.goodbye();
  flush();
}
