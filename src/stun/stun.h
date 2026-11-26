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
#include "src/strong/datagram.h"
#include "src/strong/stun/stunanswer.h"
#include "src/strong/stun/stunrequest.h"

namespace Gosh::Stun {

static const uint32_t STUN_COOKIE = 0x2112A442;

/* Whether these bytes are a STUN Binding Request one may answer: Strong::Stun::is_stun_request_checked
   (strong/stun/stunrequest.h) says, and is proved; here the datagram is made of where they are and how many. */
inline bool is_stun_request( const char* request, size_t n )
{
  return Strong::Stun::is_stun_request_checked( Strong::Datagram( reinterpret_cast<const unsigned char*>( request ), n ) );
}

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

/* If this is an answer to one that says where we were seen: true, and where. The reading is
 * Strong::Stun::StunAnswer's (strong/stun/stunanswer.h), which touches nothing of the system; here what it kept is made the
 * socket address the system knows. (Until 2026-11-18 this function did both.) */
inline bool stun_parse( const char* data, size_t len, Network::Addr& mapped )
{
  Strong::Stun::StunAnswer answer;
  if ( !answer.parse_checked( Strong::Datagram( reinterpret_cast<const unsigned char*>( data ), len ) ) ) {
    return false;
  }
  memset( &mapped, 0, sizeof( mapped ) );
  if ( answer.kept_family_checked() == 4 ) {
    mapped.sin.sin_family = AF_INET;
    mapped.sin.sin_port = htons( static_cast<uint16_t>( answer.kept_port_checked() ) );
    unsigned char v4[4];
    for ( uint32_t i = 0; i < 4; i++ ) {
      v4[i] = static_cast<unsigned char>( answer.kept_byte_checked( i ) );
    }
    memcpy( &mapped.sin.sin_addr.s_addr, v4, 4 );
  } else {
    mapped.sin6.sin6_family = AF_INET6;
    mapped.sin6.sin6_port = htons( static_cast<uint16_t>( answer.kept_port_checked() ) );
    for ( uint32_t i = 0; i < 16; i++ ) {
      mapped.sin6.sin6_addr.s6_addr[i] = static_cast<unsigned char>( answer.kept_byte_checked( i ) );
    }
  }
  return true;
}

/* "1.2.3.4:567" and "[2001:db8::1]:567" both ways. */
inline bool parse_ip_port( const std::string& text, Network::Addr& addr )
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

inline std::string format_ip_port( const Network::Addr& addr )
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
inline bool same_ip_port( const Network::Addr& a, const Network::Addr& b )
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
