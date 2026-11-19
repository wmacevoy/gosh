/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, serve: the process "gosh-server meetup PORT". An edge: a UDP socket for each family,
 * and the clock. What it does with a datagram it asks of registrations.h and answer.h; the master
 * key is master.h's. The loop is the fork's meetup_serve (mosh-1.4.0-pf.7, src/network/punch.cc),
 * with what it decided moved into those.
 *
 * (gosh, REQ17/PATH-8) The sockets are bound to every address, so each datagram is answered from
 * the address it was sent to, and one passed on goes from the address its receiver last sent to:
 * an end takes the meetup's datagrams only from the address it knows the meetup by, and a machine
 * may have several (found by review, 2026-11-16). The kernel says which, by IP_PKTINFO and
 * IPV6_PKTINFO.
 */

#define __APPLE_USE_RFC_3542 /* (macOS: IPV6_RECVPKTINFO) */

#include "src/include/config.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <map>

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
  const int on = 1;
  if ( fd >= 0 && family == AF_INET6 ) {
    setsockopt( fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof( on ) );
    setsockopt( fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof( on ) ); /* to hear which address a datagram came to */
  }
  if ( fd >= 0 && family == AF_INET ) {
    setsockopt( fd, IPPROTO_IP, IP_PKTINFO, &on, sizeof( on ) );
  }
  if ( fd >= 0 && bind( fd, res->ai_addr, res->ai_addrlen ) < 0 ) {
    close( fd );
    fd = -1;
  }
  freeaddrinfo( res );
  return fd;
}

/* One of this machine's addresses, which a datagram came to: what the kernel says, raw. */
struct Here
{
  bool known;
  struct in_pktinfo v4;
  struct in6_pktinfo v6;
};

/* A datagram, and where it came from and to. */
ssize_t receive( int fd, char* buf, size_t size, Addr& from, socklen_t& from_len, Here& here )
{
  struct iovec iov = { buf, size };
  char control[256];
  struct msghdr m;
  memset( &m, 0, sizeof( m ) );
  m.msg_name = &from.sa;
  m.msg_namelen = sizeof( from );
  m.msg_iov = &iov;
  m.msg_iovlen = 1;
  m.msg_control = control;
  m.msg_controllen = sizeof( control );
  const ssize_t n = recvmsg( fd, &m, MSG_DONTWAIT );
  from_len = m.msg_namelen;
  here.known = false;
  for ( struct cmsghdr* c = CMSG_FIRSTHDR( &m ); n >= 0 && c; c = CMSG_NXTHDR( &m, c ) ) {
    if ( c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_PKTINFO ) {
      memcpy( &here.v4, CMSG_DATA( c ), sizeof( here.v4 ) );
      here.known = true;
    } else if ( c->cmsg_level == IPPROTO_IPV6 && c->cmsg_type == IPV6_PKTINFO ) {
      memcpy( &here.v6, CMSG_DATA( c ), sizeof( here.v6 ) );
      here.known = true;
    }
  }
  return n;
}

/* Sends from the address here names, if it names one; else from whichever the system picks. */
void send_from( int fd, const std::string& bytes, const Addr& to, socklen_t to_len, const Here& here )
{
  struct iovec iov = { const_cast<char*>( bytes.data() ), bytes.size() };
  char control[256];
  memset( control, 0, sizeof( control ) );
  struct msghdr m;
  memset( &m, 0, sizeof( m ) );
  m.msg_name = const_cast<sockaddr*>( &to.sa );
  m.msg_namelen = to_len;
  m.msg_iov = &iov;
  m.msg_iovlen = 1;
  if ( here.known ) {
    m.msg_control = control;
    struct cmsghdr* c = reinterpret_cast<struct cmsghdr*>( control );
    if ( to.sa.sa_family == AF_INET ) {
      struct in_pktinfo info;
      memset( &info, 0, sizeof( info ) );
      info.ipi_spec_dst = here.v4.ipi_addr; /* (from where it came to; any interface) */
      m.msg_controllen = CMSG_SPACE( sizeof( info ) );
      c->cmsg_level = IPPROTO_IP;
      c->cmsg_type = IP_PKTINFO;
      c->cmsg_len = CMSG_LEN( sizeof( info ) );
      memcpy( CMSG_DATA( c ), &info, sizeof( info ) );
    } else {
      struct in6_pktinfo info;
      memset( &info, 0, sizeof( info ) );
      info.ipi6_addr = here.v6.ipi6_addr;
      m.msg_controllen = CMSG_SPACE( sizeof( info ) );
      c->cmsg_level = IPPROTO_IPV6;
      c->cmsg_type = IPV6_PKTINFO;
      c->cmsg_len = CMSG_LEN( sizeof( info ) );
      memcpy( CMSG_DATA( c ), &info, sizeof( info ) );
    }
  }
  sendmsg( fd, &m, 0 );
}

/* Where each end whose message was taken last sent to us: kept for passing datagrams on to it. No more than this many. */
const size_t MOST_HEARD_AT = 4 * Gosh::Registrations::MAX_TICKETS;
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
  /* (REQ17/PATH-8) By IPv4 and by IPv6, each where it can be had: a server may have only one. */
  const int socks[2] = { bound( AF_INET, port ), bound( AF_INET6, port ) };
  if ( socks[0] < 0 && socks[1] < 0 ) {
    perror( "gosh-server meetup: bind" );
    return 1;
  }

  Gosh::Registrations registrations( time( NULL ) );
  Gosh::AnswerRate rate;
  const KeyOf key_of = { master };
  std::map<std::string, Here> heard_at; /* an end's "where" -> the address of ours it sent to */
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
      Here here;
      const ssize_t n = receive( socks[s], request, sizeof( request ), from, from_len, here );
      if ( n < 0 ) {
        continue;
      }
      const std::string where = Gosh::meetup_where_of( from );
      if ( where.empty() ) {
        continue;
      }
      const uint64_t now = time( NULL );

      if ( Gosh::is_stun_request_checked( Strong::Datagram( reinterpret_cast<const unsigned char*>( request ), static_cast<uint64_t>( n ) ) ) ) {
        if ( rate.allow( now ) ) {
          send_from( socks[s], Gosh::stun_answer_to( request, where ), from, from_len, here );
        }
        continue;
      }

      const Gosh::Registrations::Send send = registrations.datagram( request, n, where, now, key_of );
      if ( !send.any ) {
        continue;
      }
      /* Taken from it: so it is an end, and this is where it knows us by. */
      if ( heard_at.size() >= MOST_HEARD_AT && heard_at.find( where ) == heard_at.end() ) {
        heard_at.clear(); /* (a passed datagram then goes from wherever the system picks, until each is heard again) */
      }
      heard_at[where] = here;
      Addr to;
      socklen_t to_len;
      if ( Gosh::meetup_addr_of( send.to, to, to_len ) ) {
        const int out = socks[to.sa.sa_family == AF_INET6 ? 1 : 0];
        const std::map<std::string, Here>::const_iterator known = heard_at.find( send.to );
        Here none;
        none.known = false;
        /* (an end's "where" and the socket it came by are of one family: what is kept for it fits) */
        if ( out >= 0 ) {
          send_from( out, send.bytes, to, to_len, known == heard_at.end() ? none : known->second );
        }
      }
    }
  }
}
