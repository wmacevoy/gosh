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
#include <sys/socket.h>
#include <sys/types.h>

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

/* "where" (messages.h) back to an IPv4 address to send to. */
bool to_address( const std::string& where, struct sockaddr_in& to )
{
  if ( where.size() != Gosh::MEETUP_WHERE_LEN || where[0] != 4 ) {
    return false;
  }
  memset( &to, 0, sizeof( to ) );
  to.sin_family = AF_INET;
  memcpy( &to.sin_addr.s_addr, where.data() + 1, 4 );
  memcpy( &to.sin_port, where.data() + 17, 2 );
  return true;
}
}

int Network::meetup_serve( const char* port, const std::string& master )
{
  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  if ( getaddrinfo( NULL, port, &hints, &res ) != 0 ) {
    fprintf( stderr, "gosh-server meetup: %s is not a port\n", port );
    return 1;
  }
  const int sock = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( sock < 0 || bind( sock, res->ai_addr, res->ai_addrlen ) < 0 ) {
    perror( "gosh-server meetup: bind" );
    return 1;
  }
  freeaddrinfo( res );

  Gosh::Registrations registrations;
  Gosh::AnswerRate rate;
  const KeyOf key_of = { master };
  while ( true ) {
    char request[65536];
    Addr from;
    socklen_t from_len = sizeof( from );
    const ssize_t n = recvfrom( sock, request, sizeof( request ), 0, &from.sa, &from_len );
    if ( n < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      return 1;
    }
    if ( from.sa.sa_family != AF_INET ) {
      continue;
    }
    const uint64_t now = time( NULL );

    if ( Gosh::is_stun_request( request, n ) ) {
      if ( rate.allow( now ) ) {
        const std::string answer
          = Gosh::stun_answer( request, ntohl( from.sin.sin_addr.s_addr ), ntohs( from.sin.sin_port ) );
        sendto( sock, answer.data(), answer.size(), 0, &from.sa, from_len );
      }
      continue;
    }

    const std::string where = Gosh::meetup_where_v4( ntohl( from.sin.sin_addr.s_addr ), ntohs( from.sin.sin_port ) );
    const Gosh::Registrations::Send send = registrations.datagram( request, n, where, now, key_of );
    struct sockaddr_in to;
    if ( send.any && to_address( send.to, to ) ) {
      sendto( sock, send.bytes.data(), send.bytes.size(), 0, reinterpret_cast<const struct sockaddr*>( &to ), sizeof( to ) );
    }
  }
}
