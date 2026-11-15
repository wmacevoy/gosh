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

/*
 * Ask the servers where datagrams from this socket appear to come from.
 * Looking their names up can take a while, and we have datagrams to
 * carry meanwhile, so another process does the asking; the answers come
 * to the socket, which is still ours to read.
 */
static void ask_stun( int sock, const std::vector<std::string>& servers )
{
  if ( servers.empty() ) {
    return;
  }
  char txid[12];
  PRNG().fill( txid, sizeof( txid ) );
  const std::string request = stun_request( txid );

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
      struct addrinfo hints, *res = NULL;
      memset( &hints, 0, sizeof( hints ) );
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      if ( getaddrinfo( servers[i].substr( 0, colon ).c_str(), servers[i].substr( colon + 1 ).c_str(), &hints, &res )
           != 0 ) {
        continue;
      }
      sendto( sock, request.data(), request.size(), 0, res->ai_addr, res->ai_addrlen );
      freeaddrinfo( res );
    }
    usleep( 400000 );
  }
  _exit( 0 );
}

Punch::Punch( bool s_initiator, const std::string& key, const std::vector<std::string>& ask, uint64_t now )
  : sock( socket( AF_INET, SOCK_DGRAM, 0 ) ), finding( s_initiator, key, now )
{
  struct sockaddr_in any;
  memset( &any, 0, sizeof( any ) );
  any.sin_family = AF_INET;
  if ( sock < 0 || bind( sock, reinterpret_cast<struct sockaddr*>( &any ), sizeof( any ) ) < 0 ) {
    if ( sock >= 0 ) {
      close( sock );
    }
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
      if ( !i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !( i->ifa_flags & IFF_UP )
           || ( i->ifa_flags & IFF_LOOPBACK ) ) {
        continue;
      }
      Addr mine;
      memset( &mine, 0, sizeof( mine ) );
      memcpy( &mine.sin, i->ifa_addr, sizeof( mine.sin ) );
      mine.sin.sin_port = htons( local_port() );
      finding.add_own_address( format_ip_port( mine ) );
    }
    freeifaddrs( interfaces );
  }

  ask_stun( sock, ask );
}

Punch::~Punch()
{
  close( sock );
}

uint16_t Punch::local_port( void ) const
{
  struct sockaddr_in sin;
  socklen_t len = sizeof( sin );
  if ( getsockname( sock, reinterpret_cast<struct sockaddr*>( &sin ), &len ) < 0 ) {
    return 0;
  }
  return ntohs( sin.sin_port );
}

/* Sends what the logic says to send. */
void Punch::flush( void )
{
  const std::vector<PathFinding::Outgoing> out = finding.take_outbox();
  for ( size_t i = 0; i < out.size(); i++ ) {
    sendto( sock, out[i].bytes.data(), out[i].bytes.size(), 0, &out[i].to.sa, sizeof( out[i].to.sin ) );
  }
}

bool Punch::use_meetup( const std::string& ip_port, const Gosh::Ticket& t )
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
  const Got got = finding.receive( now, whole, n, from, datagram );
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
