/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * path-finding, STUN (asking): the question "where do I appear to be?", its answer read, and an
 * IPv4 address and port as text (REQ1/PATH-3; design v2, 2.2, path-finding's STUN). Logic: bytes
 * and text in, bytes and addresses out. The fork's text (mosh-1.4.0-pf.7, src/network/punch.cc).
 *
 * Wrong input: an answer that is not a Binding Success Response with our cookie, or whose
 * attributes run past its end, is not read (false); text that is not "a.b.c.d:port" or
 * "[ipv6]:port" with a port from 1 to 65535 is refused, and so is an IPv6 address of one link
 * alone (fe80::/10); "[::ffff:a.b.c.d]:port" is the IPv4 address. IPv4 and IPv6 (REQ17/PATH-8).
 */

#ifndef GOSH_PATHFINDING_STUN_H
#define GOSH_PATHFINDING_STUN_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "src/network/network.h"

namespace Network {

static const uint32_t STUN_COOKIE = 0x2112A442;

/* A STUN request for our outside address: txid is 12 bytes of anything. */
inline std::string stun_request( const char* txid )
{
  /* a Binding Request with no attributes */
  std::string m;
  m.push_back( 0x00 );
  m.push_back( 0x01 );
  m.push_back( 0x00 );
  m.push_back( 0x00 );
  for ( int shift = 24; shift >= 0; shift -= 8 ) {
    m.push_back( static_cast<char>( ( STUN_COOKIE >> shift ) & 0xFF ) );
  }
  m.append( txid, 12 );
  return m;
}

/* If this is an answer to one that says where we were seen: true, and where. */
inline bool stun_parse( const char* data, size_t len, Addr& mapped )
{
  const unsigned char* p = reinterpret_cast<const unsigned char*>( data );
  /* a Binding Success Response, with our cookie in it */
  if ( len < 20 || p[0] != 0x01 || p[1] != 0x01 || p[4] != 0x21 || p[5] != 0x12 || p[6] != 0xA4 || p[7] != 0x42 ) {
    return false;
  }
  const size_t body = ( static_cast<size_t>( p[2] ) << 8 ) | p[3];
  if ( body > len - 20 ) {
    return false;
  }
  bool found = false;
  for ( size_t at = 20; at + 4 <= 20 + body; ) {
    const unsigned int type = ( p[at] << 8 ) | p[at + 1];
    const size_t size = ( static_cast<size_t>( p[at + 2] ) << 8 ) | p[at + 3];
    if ( at + 4 + size > 20 + body ) {
      break;
    }
    const unsigned char* v = p + at + 4;
    /* (gosh, REQ17/PATH-8) XOR-MAPPED-ADDRESS for IPv6: disguised with the cookie and the id, bytes 4 to 19 */
    if ( type == 0x0020 && size >= 20 && v[1] == 0x02 ) {
      Addr seen;
      memset( &seen, 0, sizeof( seen ) );
      seen.sin6.sin6_family = AF_INET6;
      seen.sin6.sin6_port = htons( ( ( v[2] << 8 ) | v[3] ) ^ ( STUN_COOKIE >> 16 ) );
      for ( int i = 0; i < 16; i++ ) {
        seen.sin6.sin6_addr.s6_addr[i] = v[4 + i] ^ p[4 + i];
      }
      /* (gosh) believed only as far as parse_ip_port would take it as text: of one link alone, not at
         all; IPv4 in IPv6's form, as the IPv4 address it is (found by the fuzzer, 2026-11-16) */
      if ( IN6_IS_ADDR_V4MAPPED( &seen.sin6.sin6_addr ) ) {
        const uint16_t port = seen.sin6.sin6_port;
        uint32_t v4;
        memcpy( &v4, seen.sin6.sin6_addr.s6_addr + 12, 4 );
        memset( &seen, 0, sizeof( seen ) );
        seen.sin.sin_family = AF_INET;
        seen.sin.sin_addr.s_addr = v4;
        seen.sin.sin_port = port;
      }
      if ( !( seen.sa.sa_family == AF_INET6 && IN6_IS_ADDR_LINKLOCAL( &seen.sin6.sin6_addr ) ) ) {
        mapped = seen;
        found = true;
      }
    }
    /* XOR-MAPPED-ADDRESS, or (from servers of an older kind) MAPPED-ADDRESS; for IPv4 */
    if ( ( type == 0x0020 || type == 0x0001 ) && size >= 8 && v[1] == 0x01 && !( found && type == 0x0001 ) ) {
      uint16_t port = ( v[2] << 8 ) | v[3];
      uint32_t ip = ( static_cast<uint32_t>( v[4] ) << 24 ) | ( v[5] << 16 ) | ( v[6] << 8 ) | v[7];
      if ( type == 0x0020 ) {
        port ^= STUN_COOKIE >> 16;
        ip ^= STUN_COOKIE;
      }
      memset( &mapped, 0, sizeof( mapped ) );
      mapped.sin.sin_family = AF_INET;
      mapped.sin.sin_port = htons( port );
      mapped.sin.sin_addr.s_addr = htonl( ip );
      found = true;
    }
    at += 4 + ( ( size + 3 ) & ~static_cast<size_t>( 3 ) );
  }
  return found;
}

/* "1.2.3.4:567" and "[2001:db8::1]:567" both ways. */
inline bool parse_ip_port( const std::string& text, Addr& addr )
{
  const size_t colon = text.rfind( ':' );
  if ( colon == std::string::npos || colon == 0 || colon + 1 >= text.size() || text.size() - colon > 6
       || text.find_first_not_of( "0123456789", colon + 1 ) != std::string::npos ) {
    return false;
  }
  const long port = atol( text.c_str() + colon + 1 );
  memset( &addr, 0, sizeof( addr ) );
  if ( port < 1 || port > 65535 ) {
    return false;
  }
  if ( text[0] == '[' ) {
    if ( colon < 3 || text[colon - 1] != ']'
         || inet_pton( AF_INET6, text.substr( 1, colon - 2 ).c_str(), &addr.sin6.sin6_addr ) != 1 ) {
      return false;
    }
    if ( IN6_IS_ADDR_LINKLOCAL( &addr.sin6.sin6_addr ) ) {
      return false; /* (gosh) of one link alone: no use without naming the link, which text here never does */
    }
    if ( IN6_IS_ADDR_V4MAPPED( &addr.sin6.sin6_addr ) ) {
      /* (gosh) an IPv4 address in IPv6's form is the IPv4 one, as the socket reports where datagrams come from */
      uint32_t v4;
      memcpy( &v4, addr.sin6.sin6_addr.s6_addr + 12, 4 );
      memset( &addr, 0, sizeof( addr ) );
      addr.sin.sin_family = AF_INET;
      addr.sin.sin_addr.s_addr = v4;
      addr.sin.sin_port = htons( port );
      return true;
    }
    addr.sin6.sin6_family = AF_INET6;
    addr.sin6.sin6_port = htons( port );
    return true;
  }
  if ( inet_pton( AF_INET, text.substr( 0, colon ).c_str(), &addr.sin.sin_addr ) != 1 ) {
    return false;
  }
  addr.sin.sin_family = AF_INET;
  addr.sin.sin_port = htons( port );
  return true;
}

inline std::string format_ip_port( const Addr& addr )
{
  char ip[INET6_ADDRSTRLEN];
  if ( addr.sa.sa_family == AF_INET && inet_ntop( AF_INET, &addr.sin.sin_addr, ip, sizeof( ip ) ) ) {
    return std::string( ip ) + ":" + std::to_string( ntohs( addr.sin.sin_port ) );
  }
  if ( addr.sa.sa_family == AF_INET6 && inet_ntop( AF_INET6, &addr.sin6.sin6_addr, ip, sizeof( ip ) ) ) {
    return "[" + std::string( ip ) + "]:" + std::to_string( ntohs( addr.sin6.sin6_port ) );
  }
  return std::string();
}

/* Whether two addresses and ports are the same: of one family, with the same address and port. */
inline bool same_ip_port( const Addr& a, const Addr& b )
{
  if ( a.sa.sa_family != b.sa.sa_family ) {
    return false;
  }
  if ( a.sa.sa_family == AF_INET6 ) {
    return memcmp( &a.sin6.sin6_addr, &b.sin6.sin6_addr, 16 ) == 0 && a.sin6.sin6_port == b.sin6.sin6_port;
  }
  return a.sin.sin_addr.s_addr == b.sin.sin_addr.s_addr && a.sin.sin_port == b.sin.sin_port;
}

}

#endif
