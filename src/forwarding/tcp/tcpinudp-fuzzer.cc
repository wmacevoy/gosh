/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/FWD-1: fuzzes the TCP-in-UDP header transformation (tcpinudp.h): parsing a segment in each
 * of its forms, and checking that whatever parses can be written out again and read back to the
 * same segment.
 *
 * (gosh) The fork's src/fuzz/tcpinudp_fuzzer.cc (mosh-1.4.0-pf.7); only its includes are changed.
 * Its corpus is the fork's.
 */

#include "src/forwarding/fuzz.h"
#include "src/forwarding/tcp/tcpinudp.h"
#include "src/seam/fuzz-replay.h"

using namespace Network;
using namespace ForwardFuzz;

static bool same( const TCPInUDP& a, const TCPInUDP& b )
{
  return a.source_port == b.source_port && a.dest_port == b.dest_port && a.seq == b.seq && a.ack == b.ack
         && a.flags == b.flags && a.window == b.window && a.options == b.options && a.payload == b.payload;
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  Input in( data, size );

  Addr from4, to4, from6, to6;
  memset( &from4, 0, sizeof( from4 ) );
  memset( &to4, 0, sizeof( to4 ) );
  memset( &from6, 0, sizeof( from6 ) );
  memset( &to6, 0, sizeof( to6 ) );
  from4.sin.sin_family = to4.sin.sin_family = AF_INET;
  from6.sin6.sin6_family = to6.sin6.sin6_family = AF_INET6;
  from4.sin.sin_addr.s_addr = in.byte() * 0x01010101u;
  to4.sin.sin_addr.s_addr = in.byte() * 0x01000001u;
  from6.sin6.sin6_addr.s6_addr[0] = in.byte();
  to6.sin6.sin6_addr.s6_addr[15] = in.byte();
  const bool v6 = in.byte() & 1;
  const Addr& from = v6 ? from6 : from4;
  const Addr& to = v6 ? to6 : to4;
  const uint16_t source_port = in.byte() * 257, dest_port = in.byte() * 257;

  const std::string rest = in.bytes( 60000 ); /* (a segment can't be longer than an IP packet) */

  TCPInUDP a, b;

  /* as it arrives in a forwarded datagram */
  if ( TCPInUDP::from_datagram( source_port, dest_port, rest, a ) ) {
    require( TCPInUDP::from_datagram( source_port, dest_port, a.to_datagram(), b ) && same( a, b ) );
    require( TCPInUDP::from_tcp( a.to_tcp( from, to ), b ) && same( a, b ) );
    if ( rest.size() + TCPInUDP::UDP_HEADER_LEN <= 0xFFFF ) {
      const std::string udp = a.to_udp( from, to );
      require( udp.size() == a.to_tcp( from, to ).size() ); /* no bytes added: that is the point */
      require( TCPInUDP::from_datagram( source_port, dest_port, udp.substr( TCPInUDP::UDP_HEADER_LEN ), b )
               && same( a, b ) );
    }
  }

  /* as a real TCP would send it */
  if ( TCPInUDP::from_tcp( rest, a ) ) {
    require( TCPInUDP::from_tcp( a.to_tcp( from, to ), b ) && same( a, b ) );
    require( TCPInUDP::from_datagram( a.source_port, a.dest_port, a.to_datagram(), b ) && same( a, b ) );
  }

  TCPInUDP::max_segment_size( rest.size() );
  return 0;
}
