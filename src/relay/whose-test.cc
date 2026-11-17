/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/PATH-1: whose a datagram is (whose.h), with addresses and text and no socket. */

#include <cstdio>
#include <cstdlib>
#include <string>

#include <arpa/inet.h>

#include "src/relay/whose.h"
#include "vendor/facts/facts.h"



static struct sockaddr_in v4( const char* address, uint16_t port )
{
  struct sockaddr_in a;
  memset( &a, 0, sizeof( a ) );
  a.sin_family = AF_INET;
  a.sin_port = htons( port );
  inet_pton( AF_INET, address, &a.sin_addr );
  return a;
}

static struct sockaddr_in6 v6( const char* address, uint16_t port )
{
  struct sockaddr_in6 a;
  memset( &a, 0, sizeof( a ) );
  a.sin6_family = AF_INET6;
  a.sin6_port = htons( port );
  inet_pton( AF_INET6, address, &a.sin6_addr );
  return a;
}

template<class A, class B>
static bool same( const A& a, const B& b )
{
  return Gosh::same_udp_address( reinterpret_cast<const struct sockaddr*>( &a ), sizeof( a ),
                                 reinterpret_cast<const struct sockaddr*>( &b ), sizeof( b ) );
}

/* The kernel's list, as this machine wrote it on 2026-11-15, with the numbers changed. */
static const char LIST[] =
  "   sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops            \n"
  " 1391: 00000000:EA60 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 116989101 2 000000002d32109d 0     \n"
  " 1392: 0100007F:EA61 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 117525914 2 00000000df04f88e 0     \n"
  " 1400: 0100007F:C350 00000000:0000 07 00000000:00000000 00:00000000 00000000  1001        0 117525915 2 00000000df04f88f 0     \n"
  " 1401: 00000000:C351 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 117525916 2 00000000df04f890 0     \n"
  " 1402: 0100007F:C351 00000000:0000 07 00000000:00000000 00:00000000 00000000  1001        0 117525917 2 00000000df04f891 0     \n"
  " 1403: 0200007F:C352 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 117525918 2 00000000df04f892 0     \n"
  " cut short\n";
/* REQ1/PATH-1: whose a datagram is */
FACTS( Whose )
{
  FACT_CHECK( same( v4( "198.51.100.9", 60001 ), v4( "198.51.100.9", 60001 ) ), "the same IPv4 address and port are the same place" );
  FACT_CHECK( !same( v4( "198.51.100.9", 60001 ), v4( "198.51.100.9", 60002 ) ), "another port is not" );
  FACT_CHECK( !same( v4( "198.51.100.9", 60001 ), v4( "198.51.100.8", 60001 ) ), "another address is not" );
  FACT_CHECK( same( v6( "2001:db8::9", 60001 ), v6( "2001:db8::9", 60001 ) ), "the same IPv6 address and port are" );
  FACT_CHECK( ( !same( v6( "2001:db8::9", 60001 ), v6( "2001:db8::8", 60001 ) ) && !same( v6( "2001:db8::9", 60001 ), v6( "2001:db8::9", 1 ) ) ), "another IPv6 address or port is not" );
  FACT_CHECK( !same( v4( "127.0.0.1", 60001 ), v6( "::1", 60001 ) ), "an IPv4 and an IPv6 address are never the same place" );
  const struct sockaddr_in whole = v4( "127.0.0.1", 9 );
  FACT_CHECK( !Gosh::same_udp_address( reinterpret_cast<const struct sockaddr*>( &whole ), 4,
                                  reinterpret_cast<const struct sockaddr*>( &whole ), sizeof( whole ) ), "an address cut short is no place" );

  const uint32_t loopback = v4( "127.0.0.1", 0 ).sin_addr.s_addr;
  using Gosh::udp_sender_is_users;
  FACT_CHECK( udp_sender_is_users( LIST, loopback, 0xEA60, 1000 ), "a port held by a socket of ours on any address: ours" );
  FACT_CHECK( udp_sender_is_users( LIST, loopback, 0xEA61, 1000 ), "a port held by a socket of ours on that address: ours" );
  FACT_CHECK( !udp_sender_is_users( LIST, loopback, 0xC350, 1000 ), "a port held by another user's socket: not ours" );
  FACT_CHECK( udp_sender_is_users( LIST, loopback, 0xC350, 1001 ), "(and theirs, asked as them)" );
  FACT_CHECK( ( !udp_sender_is_users( LIST, loopback, 0xC351, 1000 ) && !udp_sender_is_users( LIST, loopback, 0xC351, 1001 ) ), "a port held by ours and by another user's that could both have sent it: not ours" );
  FACT_CHECK( !udp_sender_is_users( LIST, loopback, 0xC352, 1000 ), "a port held by ours on another address: could not have sent it, so not ours" );
  FACT_CHECK( !udp_sender_is_users( LIST, loopback, 0x1234, 1000 ), "a port nobody holds: not ours" );
  FACT_CHECK( ( !udp_sender_is_users( "", loopback, 0xEA60, 1000 ) && !udp_sender_is_users( "one\ntwo words\n", loopback, 0xEA60, 1000 ) ), "an empty list, or one of nothing readable: not ours" );

  FACT_CHECK( Gosh::remote_ways_kept( 4, 0 ) == 1, "four ways to the server and the first answers: one is kept" );
  FACT_CHECK( Gosh::remote_ways_kept( 4, 2 ) == 3, "the third answers: three are kept, the two more likely among them" );
  FACT_CHECK( Gosh::remote_ways_kept( 4, 3 ) == 4, "the last answers: all are kept" );
  FACT_CHECK( Gosh::remote_ways_kept( 1, 0 ) == 1, "one way, and it answers: it is kept" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Whose );
}

FACTS_MAIN
