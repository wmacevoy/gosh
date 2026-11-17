/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* Tests the TCP-in-UDP header transformation: both directions, the
   checksums against independently computed values, and rejection of
   segments that are malformed or cannot be represented. */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "src/forwarding/tcp/tcpinudp.h"
#include "src/forwarding/frame.h"
#include "vendor/facts/facts.h"

using namespace Network;

/* (gosh) The fork's UDPForwarder::encode and ::decode, which only its tests called: a frame of plain UDP data. */
static std::string encode_data( uint8_t forward, uint16_t flow_id, uint64_t seq, const std::string& datagram )
{
  return Gosh::encode_frame( Gosh::TYPE_UDP_DATA, forward, flow_id, seq, datagram );
}

static bool decode_data( const std::string& payload, uint8_t& forward, uint16_t& flow_id, uint64_t& seq, std::string& datagram )
{
  uint8_t type;
  return Gosh::decode_frame( payload, type, forward, flow_id, seq, datagram ) && type == Gosh::TYPE_UDP_DATA;
}


static Addr addr4( const char* s )
{
  Facts* facts = FactsCurrent(); /* the group running */
  Addr a;
  memset( &a, 0, sizeof( a ) );
  a.sin.sin_family = AF_INET;
  FACT_CHECK( inet_pton( AF_INET, s, &a.sin.sin_addr ) == 1 );
  return a;
}

static Addr addr6( const char* s )
{
  Facts* facts = FactsCurrent(); /* the group running */
  Addr a;
  memset( &a, 0, sizeof( a ) );
  a.sin6.sin6_family = AF_INET6;
  FACT_CHECK( inet_pton( AF_INET6, s, &a.sin6.sin6_addr ) == 1 );
  return a;
}

static std::string bytes( const unsigned char* p, size_t n )
{
  return std::string( reinterpret_cast<const char*>( p ), n );
}

static unsigned int word( const std::string& s, size_t at )
{
  return ( static_cast<unsigned char>( s[at] ) << 8 ) | static_cast<unsigned char>( s[at + 1] );
}

/* IPv4 SYN carrying an MSS option, 192.0.2.1:12345 -> 198.51.100.2:80 */
static const unsigned char syn_tcp[]
  = { 0x30, 0x39, 0x00, 0x50,                         /* ports */
      0x01, 0x02, 0x03, 0x04,                         /* seq */
      0x00, 0x00, 0x00, 0x00,                         /* ack */
      0x60, 0x02, 0xFF, 0xFF,                         /* offset 6, SYN; window */
      0x77, 0x60, 0x00, 0x00,                         /* checksum; urgent pointer */
      0x02, 0x04, 0x05, 0xB4 };                       /* MSS 1460 */
static const unsigned char syn_udp[]
  = { 0x30, 0x39, 0x00, 0x50,                         /* ports */
      0x00, 0x18, 0x77, 0x3D,                         /* length 24; checksum */
      0x60, 0x02, 0xFF, 0xFF,                         /* offset 6, SYN; window */
      0x01, 0x02, 0x03, 0x04,                         /* seq */
      0x00, 0x00, 0x00, 0x00,                         /* ack */
      0x02, 0x04, 0x05, 0xB4 };                       /* MSS 1460 */
FACTS( Syn )
{
  const Addr src = addr4( "192.0.2.1" ), dst = addr4( "198.51.100.2" );
  const std::string tcp = bytes( syn_tcp, sizeof( syn_tcp ) );
  const std::string udp = bytes( syn_udp, sizeof( syn_udp ) );

  TCPInUDP s;
  FACT_CHECK( TCPInUDP::from_tcp( tcp, s ) );
  FACT_CHECK( ( s.source_port == 12345 && s.dest_port == 80 ) );
  FACT_CHECK( ( s.seq == 0x01020304 && s.ack == 0 && s.window == 0xFFFF ) );
  FACT_CHECK( ( s.flags == TCPInUDP::SYN && s.has( TCPInUDP::SYN ) && !s.has( TCPInUDP::ACK ) ) );
  FACT_CHECK( ( s.data_offset() == 6 && s.options == std::string( "\x02\x04\x05\xB4" ) && s.payload.empty() ) );

  /* TCP -> UDP: same bytes rearranged, same length */
  FACT_CHECK( s.to_udp( src, dst ) == udp );
  FACT_CHECK( s.to_datagram() == udp.substr( TCPInUDP::UDP_HEADER_LEN ) );
  FACT_CHECK( udp.size() == tcp.size() );

  /* UDP -> TCP, as a socket would see it: the payload plus the datagram's ports */
  TCPInUDP r;
  FACT_CHECK( TCPInUDP::from_datagram( 12345, 80, udp.substr( TCPInUDP::UDP_HEADER_LEN ), r ) );
  FACT_CHECK( r.to_tcp( src, dst ) == tcp );

  /* The forwarder acts as a NAT: other addresses and ports, another (valid) checksum. */
  r.source_port = 40000;
  const std::string natted = r.to_tcp( addr4( "10.0.0.1" ), dst );
  FACT_CHECK( ( natted != tcp && natted.size() == tcp.size() ) );
  std::string zeroed = natted;
  zeroed[16] = zeroed[17] = 0;
  FACT_CHECK( TCPInUDP::checksum( IPPROTO_TCP, addr4( "10.0.0.1" ), dst, zeroed ) == word( natted, 16 ) );
}
FACTS( DataIpv6 )
{
  /* PSH|ACK with an odd-length payload, [2001:db8::1]:443 -> [2001:db8::2]:50000 */
  const Addr src = addr6( "2001:db8::1" ), dst = addr6( "2001:db8::2" );

  TCPInUDP s;
  s.source_port = 443;
  s.dest_port = 50000;
  s.seq = 0xDEADBEEF;
  s.ack = 0x00C0FFEE;
  s.flags = TCPInUDP::PSH | TCPInUDP::ACK;
  s.window = 1024;
  s.payload = "hello";

  const std::string tcp = s.to_tcp( src, dst );
  const std::string udp = s.to_udp( src, dst );
  FACT_CHECK( ( tcp.size() == 25 && udp.size() == 25 ) );
  FACT_CHECK( word( tcp, 16 ) == 0xA928 ); /* computed independently */
  FACT_CHECK( word( udp, 6 ) == 0xA904 );
  FACT_CHECK( word( udp, 4 ) == 25 );
  FACT_CHECK( ( word( tcp, 12 ) == 0x5018 && word( udp, 8 ) == 0x5018 ) );

  /*
   * The kernel translator never recomputes a checksum; it only adjusts
   * it for what changed: the protocol number (6 -> 17) in the
   * pseudo-header, and the UDP length taking the place of a zero
   * urgent pointer.  The two forms must agree with that.
   */
  unsigned int sum = ( ~word( tcp, 16 ) & 0xFFFF ) + ( IPPROTO_UDP - IPPROTO_TCP ) + udp.size();
  sum = ( sum & 0xFFFF ) + ( sum >> 16 );
  FACT_CHECK( ( ~sum & 0xFFFF ) == word( udp, 6 ) );

  TCPInUDP r;
  FACT_CHECK( TCPInUDP::from_tcp( tcp, r ) );
  FACT_CHECK( r.to_udp( src, dst ) == udp );
  FACT_CHECK( TCPInUDP::from_datagram( 443, 50000, udp.substr( 8 ), r ) );
  FACT_CHECK( ( r.payload == "hello" && r.options.empty() && r.to_tcp( src, dst ) == tcp ) );

  /* a checksum needs one address family */
  bool threw = false;
  try {
    s.to_tcp( src, addr4( "192.0.2.1" ) );
  } catch ( const NetworkException& ) {
    threw = true;
  }
  FACT_CHECK( threw );
}
FACTS( Rejects )
{
  const std::string tcp = bytes( syn_tcp, sizeof( syn_tcp ) );
  const std::string datagram = bytes( syn_udp, sizeof( syn_udp ) ).substr( 8 );
  TCPInUDP s;

  /* truncated */
  FACT_CHECK( !TCPInUDP::from_tcp( tcp.substr( 0, 19 ), s ) );
  FACT_CHECK( !TCPInUDP::from_datagram( 1, 2, datagram.substr( 0, 11 ), s ) );
  FACT_CHECK( !TCPInUDP::from_datagram( 1, 2, "", s ) );

  /* header claims options that are not there */
  FACT_CHECK( !TCPInUDP::from_tcp( tcp.substr( 0, 20 ), s ) );
  FACT_CHECK( !TCPInUDP::from_datagram( 1, 2, datagram.substr( 0, 12 ), s ) );

  /* data offset smaller than a TCP header */
  std::string bad = tcp;
  bad[12] = 0x40;
  FACT_CHECK( !TCPInUDP::from_tcp( bad, s ) );
  bad = datagram;
  bad[0] = 0x40;
  FACT_CHECK( !TCPInUDP::from_datagram( 1, 2, bad, s ) );

  /* urgent data has nowhere to go */
  bad = tcp;
  bad[13] |= 0x20;
  FACT_CHECK( !TCPInUDP::from_tcp( bad, s ) );
  /* ...and a stray URG bit arriving in a datagram is dropped, not believed */
  bad = datagram;
  bad[1] |= 0x20;
  FACT_CHECK( TCPInUDP::from_datagram( 1, 2, bad, s ) );
  FACT_CHECK( ( !s.has( TCPInUDP::URG ) && s.has( TCPInUDP::SYN ) ) );

  /* can't serialize what can't be represented */
  const char* unrepresentable[] = { "urg", "ragged options", "too many options" };
  for ( size_t i = 0; i < 3; i++ ) {
    TCPInUDP x;
    if ( i == 0 ) {
      x.flags = TCPInUDP::URG;
    } else if ( i == 1 ) {
      x.options = "abc";
    } else {
      x.options = std::string( 44, '\x01' );
    }
    bool threw = false;
    try {
      x.to_datagram();
    } catch ( const NetworkException& ) {
      threw = true;
    }
    if ( !threw ) {
      fprintf( stderr, "accepted: %s\n", unrepresentable[i] );
    }
    FACT_CHECK( threw );
  }
}
FACTS( Sizes )
{
  /* a full-sized segment with full options exactly fills the budget */
  const size_t budget = 1200 - Gosh::FRAME_HEADER_LEN;
  const size_t mss = TCPInUDP::max_segment_size( budget );
  FACT_CHECK( mss == budget - 12 - 40 );

  TCPInUDP s;
  s.flags = TCPInUDP::ACK;
  s.options = std::string( TCPInUDP::MAX_OPTIONS_LEN, '\x01' ); /* NOPs */
  s.payload = std::string( mss, 'x' );
  FACT_CHECK( s.data_offset() == 15 );
  FACT_CHECK( s.to_datagram().size() == budget );

  FACT_CHECK( ( TCPInUDP::max_segment_size( 52 ) == 0 && TCPInUDP::max_segment_size( 0 ) == 0 ) );

  /* and it survives the forwarder's own framing untouched */
  const std::string wire = encode_data( 0, 1, 2, s.to_datagram() );
  FACT_CHECK( wire.size() == 1200 );
  uint8_t forward;
  uint16_t flow;
  uint64_t seq;
  std::string datagram;
  TCPInUDP r;
  FACT_CHECK( decode_data( wire, forward, flow, seq, datagram ) );
  FACT_CHECK( TCPInUDP::from_datagram( 0, 0, datagram, r ) );
  FACT_CHECK( ( r.payload == s.payload && r.options == s.options && r.flags == s.flags ) );
}
FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Syn );
  FACTS_REGISTER( DataIpv6 );
  FACTS_REGISTER( Rejects );
  FACTS_REGISTER( Sizes );
}

FACTS_MAIN
