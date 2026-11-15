/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, serve: the process "gosh-server meetup PORT". An edge: one UDP socket and the clock.
 * What it does with a datagram it asks of passing.h and answer.h, and whether twelve bytes show
 * the key of proof.h. The fork's meetup_serve (mosh-1.4.0-pf.7, src/network/punch.cc), with what
 * it decided moved into those.
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
#include "src/meetup/passing.h"
#include "src/meetup/proof.h"
#include "src/network/network.h"

using namespace Network;

namespace {
/* Whether twelve bytes show the key; anything that goes wrong in finding out is a no. */
struct ShowsKey
{
  const std::string& key;
  bool operator()( const char* twelve ) const
  {
    try {
      return meetup_proof_ok( twelve, key );
    } catch ( const std::exception& e ) {
      return false;
    }
  }
};
}

int Network::meetup_serve( const char* port, const std::string& key )
{
  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  if ( getaddrinfo( NULL, port, &hints, &res ) != 0 ) {
    fprintf( stderr, "mosh-server meetup: %s is not a port\n", port );
    return 1;
  }
  const int sock = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( sock < 0 || bind( sock, res->ai_addr, res->ai_addrlen ) < 0 ) {
    perror( "mosh-server meetup: bind" );
    return 1;
  }
  freeaddrinfo( res );
  if ( !key.empty() ) {
    try {
      meetup_proof( key );
    } catch ( const std::exception& e ) {
      fprintf( stderr, "mosh-server meetup: MOSH_MEETUP_KEY is not a key (mosh-server meetup-key makes one)\n" );
      return 1;
    }
  }

  Gosh::Passing passing;
  Gosh::AnswerRate rate;
  const ShowsKey shows_key = { key };
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

    /* To be passed on?  Only for those with the key. */
    if ( !key.empty() && Gosh::Passing::is_for_passing( request, n ) ) {
      std::string to;
      const std::string sender( reinterpret_cast<const char*>( &from.sin ), sizeof( from.sin ) );
      if ( passing.datagram( request, n, sender, time( NULL ), shows_key, to ) ) {
        sendto( sock, request, n, 0, reinterpret_cast<const struct sockaddr*>( to.data() ), to.size() );
      }
      continue;
    }
    if ( !Gosh::is_stun_request( request, n ) || !rate.allow( time( NULL ) ) ) {
      continue;
    }
    if ( !key.empty() && !shows_key( request + 8 ) ) {
      continue; /* not one of ours: and no answer says so, or that we are here at all */
    }
    const std::string answer = Gosh::stun_answer( request, ntohl( from.sin.sin_addr.s_addr ), ntohs( from.sin.sin_port ) );
    sendto( sock, answer.data(), answer.size(), 0, &from.sa, from_len );
  }
}
