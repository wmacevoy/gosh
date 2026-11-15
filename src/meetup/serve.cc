/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, serve: the process "gosh-server meetup PORT". An edge: one UDP socket and the clock.
 * What it does with a datagram it asks of registrations.h and answer.h; the master key is
 * master.h's. The loop is the fork's meetup_serve (mosh-1.4.0-pf.7, src/network/punch.cc), with
 * what it decided moved into those.
 */

#include "src/include/config.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>

#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/meetup/answer.h"
#include "src/meetup/meetup.h"
#include "src/meetup/registrations.h"
#include "src/meetup/ticket.h"
#include "src/network/network.h"

using namespace Network;

namespace {
/* A ticket's key, under the master key; anything that goes wrong in working it out is no key. */
struct KeyOf
{
  const std::string& master;
  std::string operator()( const std::string& id ) const
  {
    try {
      return Gosh::ticket_key( master, id );
    } catch ( const std::exception& ) {
      return std::string();
    }
  }
};

/* A socket of this family bound to the port on every address; -1 if there can be none. IPv6 only on the IPv6 one. */
int bound( int family, const char* port )
{
  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = family;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  if ( getaddrinfo( NULL, port, &hints, &res ) != 0 ) {
    return -1;
  }
  int fd = socket( family, SOCK_DGRAM, 0 );
  if ( fd >= 0 && family == AF_INET6 ) {
    const int only = 1;
    setsockopt( fd, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof( only ) );
  }
  if ( fd >= 0 && bind( fd, res->ai_addr, res->ai_addrlen ) < 0 ) {
    close( fd );
    fd = -1;
  }
  freeaddrinfo( res );
  return fd;
}
}

int Network::meetup_serve( const char* port, const std::string& master )
{
  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  hints.ai_socktype = SOCK_DGRAM;
  if ( getaddrinfo( NULL, port, &hints, &res ) != 0 ) {
    fprintf( stderr, "gosh-server meetup: %s is not a port\n", port );
    return 1;
  }
  freeaddrinfo( res );
  /* (REQ16/PATH-8) By IPv4 and by IPv6, each where it can be had: a server may have only one. */
  const int socks[2] = { bound( AF_INET, port ), bound( AF_INET6, port ) };
  if ( socks[0] < 0 && socks[1] < 0 ) {
    perror( "gosh-server meetup: bind" );
    return 1;
  }

  Gosh::Registrations registrations;
  Gosh::AnswerRate rate;
  const KeyOf key_of = { master };
  while ( true ) {
    struct pollfd fds[2] = { { socks[0], POLLIN, 0 }, { socks[1], POLLIN, 0 } };
    if ( poll( fds, 2, -1 ) < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      return 1;
    }
    for ( int s = 0; s < 2; s++ ) {
      if ( socks[s] < 0 || !( fds[s].revents & POLLIN ) ) {
        continue;
      }
      char request[65536];
      Addr from;
      socklen_t from_len = sizeof( from );
      const ssize_t n = recvfrom( socks[s], request, sizeof( request ), MSG_DONTWAIT, &from.sa, &from_len );
      if ( n < 0 ) {
        continue;
      }
      const std::string where = Gosh::meetup_where_of( from );
      if ( where.empty() ) {
        continue;
      }
      const uint64_t now = time( NULL );

      if ( Gosh::is_stun_request( request, n ) ) {
        if ( rate.allow( now ) ) {
          const std::string answer = Gosh::stun_answer_to( request, where );
          sendto( socks[s], answer.data(), answer.size(), 0, &from.sa, from_len );
        }
        continue;
      }

      const Gosh::Registrations::Send send = registrations.datagram( request, n, where, now, key_of );
      Addr to;
      socklen_t to_len;
      if ( send.any && Gosh::meetup_addr_of( send.to, to, to_len ) ) {
        const int out = socks[to.sa.sa_family == AF_INET6 ? 1 : 0];
        if ( out >= 0 ) {
          sendto( out, send.bytes.data(), send.bytes.size(), 0, &to.sa, to_len );
        }
      }
    }
  }
}
