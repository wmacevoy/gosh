/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * relay, whose: is a datagram from who it should be from? (REQ1/PATH-1; FINDINGS 2026-11-14,
 * "five things".) Logic: addresses and text in, yes or no out. local.cc and remote.cc ask before it believes
 * a datagram.
 *
 * The relays pass on sealed datagrams and cannot tell a true one from a false one; the session
 * can, and refuses what is not its own. What a relay must not do is change its mind about
 * where things go because of a datagram anyone could have sent. Three rules:
 *
 *   from the server    A datagram on the local relay's direct socket counts as the server
 *                      answering, and is passed to the client, only if it comes from the
 *                      server's address and port.
 *
 *   from our client    The local relay's client is a process of the same user on the same
 *                      machine, talking to it on loopback from a port that changes. A datagram
 *                      from a new port is taken for the client's only if the kernel's list of
 *                      UDP sockets says a socket of this user's could have sent it, and no
 *                      socket of another user's could. Where there is no such list to read
 *                      (not Linux), it cannot be told, and the datagram is taken, as the fork
 *                      took every one: docs say so (REQ1/SHAPE-4).
 *
 *   which way to the server
 *                      The remote relay tries several addresses for the server, the likeliest
 *                      first. When one answers, the ways after it are given up; the ways before
 *                      it are kept and still tried, so that an answer from a less likely place
 *                      never shuts out a more likely one.
 */

#ifndef GOSH_RELAY_WHOSE_H
#define GOSH_RELAY_WHOSE_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

namespace Gosh::Relay::Imp {

/* Whether two socket addresses are the same place: family, address and port (IPv4 or IPv6). */
inline bool same_udp_address( const struct sockaddr* a, socklen_t a_len, const struct sockaddr* b, socklen_t b_len )
{
  if ( a_len < sizeof( sa_family_t ) || b_len < sizeof( sa_family_t ) || a->sa_family != b->sa_family ) {
    return false;
  }
  if ( a->sa_family == AF_INET ) {
    if ( a_len < sizeof( struct sockaddr_in ) || b_len < sizeof( struct sockaddr_in ) ) {
      return false;
    }
    const struct sockaddr_in* x = reinterpret_cast<const struct sockaddr_in*>( a );
    const struct sockaddr_in* y = reinterpret_cast<const struct sockaddr_in*>( b );
    return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
  }
  if ( a->sa_family == AF_INET6 ) {
    if ( a_len < sizeof( struct sockaddr_in6 ) || b_len < sizeof( struct sockaddr_in6 ) ) {
      return false;
    }
    const struct sockaddr_in6* x = reinterpret_cast<const struct sockaddr_in6*>( a );
    const struct sockaddr_in6* y = reinterpret_cast<const struct sockaddr_in6*>( b );
    return x->sin6_port == y->sin6_port && memcmp( &x->sin6_addr, &y->sin6_addr, sizeof( x->sin6_addr ) ) == 0;
  }
  return false;
}

/*
 * Whether a datagram from this IPv4 address and port was sent by a socket of this user's.
 *   sockets   the kernel's list of UDP sockets, as Linux gives it in /proc/net/udp: a heading,
 *             then a line for each socket, whose second word is its own address and port in
 *             hexadecimal ("0100007F:EA60"; all noughts for an address means any) and whose
 *             eighth is the number of the user it belongs to
 *   s_addr    the address, as it is in a sockaddr_in (the list writes it the same way)
 *   port      the port, as a number
 * Yes only if some socket in the list could have sent it (that port; that address, or any) and
 * every such socket is this user's. A line that cannot be read is passed over.
 */
inline bool udp_sender_is_users( const std::string& sockets, uint32_t s_addr, uint16_t port, unsigned long user )
{
  bool ours = false;
  size_t at = 0;
  while ( at < sockets.size() ) {
    size_t end = sockets.find( '\n', at );
    if ( end == std::string::npos ) {
      end = sockets.size();
    }
    /* the words of this line */
    std::string words[8];
    size_t count = 0;
    for ( size_t i = at; i < end && count < 8; ) {
      while ( i < end && sockets[i] == ' ' ) {
        i++;
      }
      size_t j = i;
      while ( j < end && sockets[j] != ' ' ) {
        j++;
      }
      if ( j > i ) {
        words[count++] = sockets.substr( i, j - i );
      }
      i = j;
    }
    at = end + 1;
    if ( count < 8 ) {
      continue;
    }
    const size_t colon = words[1].find( ':' );
    if ( colon != 8 || words[1].size() < 10 ) {
      continue; /* (the heading; or not an IPv4 socket's line) */
    }
    char* stop = NULL;
    const unsigned long its_address = strtoul( words[1].substr( 0, 8 ).c_str(), &stop, 16 );
    if ( *stop ) {
      continue;
    }
    const unsigned long its_port = strtoul( words[1].substr( 9 ).c_str(), &stop, 16 );
    if ( *stop ) {
      continue;
    }
    const unsigned long its_user = strtoul( words[7].c_str(), &stop, 10 );
    if ( *stop || words[7].empty() ) {
      continue;
    }
    if ( its_port != port || ( its_address != 0 && its_address != s_addr ) ) {
      continue;
    }
    if ( its_user != user ) {
      return false;
    }
    ours = true;
  }
  return ours;
}

/*
 * The remote relay has `count` ways to the server open, the likeliest first, and the one at
 * `answered` (counting from 0) has just answered. How many to keep: it and those before it.
 */
inline size_t remote_ways_kept( size_t count, size_t answered )
{
  return answered + 1 < count ? answered + 1 : count;
}

}

#endif
