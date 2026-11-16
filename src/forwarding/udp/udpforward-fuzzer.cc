/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/FWD-2: fuzzes the UDP forwarder (udpforward.h, around conversations.h's logic) as the
 * connecting end sees it: everything that can arrive on the side channel (frames, fragments,
 * replays, junk), and replies of every size from the forwarding target, which it must fragment
 * to send back.
 *
 * It uses real sockets on the loopback interface, since the forwarder does. Nothing reads what it
 * sends to the "target", which is fine.
 *
 * (gosh) The fork's src/fuzz/udp_forward_fuzzer.cc (mosh-1.4.0-pf.7); the frame's functions and
 * numbers are frame.h's now, and reassembly fragments.h's. Its corpus is the fork's.
 */

#include <vector>

#include "src/forwarding/frame.h"
#include "src/forwarding/fuzz.h"
#include "src/forwarding/udp/fragments.h"
#include "src/forwarding/udp/udpforward.h"
#include "src/seam/fuzz-replay.h"
#include "src/util/timestamp.h"

using namespace Network;
using namespace ForwardFuzz;

namespace {

class Link : public SideChannelLink
{
public:
  size_t limit;
  double loss;
  std::vector<std::string> sent;

  Link() : limit( 1200 ), loss( 0 ), sent() {}

  bool sidechannel_ready( void ) const { return true; }
  size_t sidechannel_max_payload( void ) const { return limit; }
  double sidechannel_loss( void ) const { return loss; }
  bool sidechannel_connected( void ) const { return true; }
  void sidechannel_send( const std::string& payload )
  {
    /* whatever it sends must fit, and must be a frame */
    require( payload.size() <= limit && payload.size() >= Gosh::FRAME_HEADER_LEN );
    require( static_cast<uint8_t>( payload[0] ) == SIDECHANNEL_MAGIC );
    if ( sent.size() < 64 ) {
      sent.push_back( payload );
    }
  }
};

int target = -1;
uint16_t target_port;
struct sockaddr_in target_addr;

}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  if ( target < 0 ) {
    target = loopback_socket( SOCK_DGRAM, target_port, target_addr );
    set_nonblocking( target );
  }

  Input in( data, size );
  freeze_timestamp();

  Link link;
  link.limit = Gosh::FRAME_HEADER_LEN + Gosh::DatagramAssembly::FRAGMENT_HEADER_LEN + 8 + in.byte() * 5;
  link.loss = in.byte() / 255.0;
  const unsigned int mode = in.byte() % 3;

  char spec[64];
  snprintf( spec, sizeof( spec ), "1:127.0.0.1:%d", target_port );
  UDPForwarder forwarder( &link, UDPForwarder::CONNECT, std::vector<std::string>( 2, spec ) );
  forwarder.set_parity_mode( mode == 0 ? UDPForwarder::PARITY_AUTO
                                       : mode == 1 ? UDPForwarder::PARITY_ALWAYS : UDPForwarder::PARITY_NEVER );

  uint64_t seq = 1000;
  char buf[65536];

  unsigned int kind;
  std::string body;
  while ( in.record( kind, body ) ) {
    switch ( kind ) {
      case 0: /* anything at all */
        forwarder.sidechannel_recv( body );
        break;

      case 1: { /* a frame that at least looks like one */
        seq += static_cast<int8_t>( at( body, 3 ) );
        const std::string payload = body.substr( body.size() < 4 ? body.size() : 4 );
        forwarder.sidechannel_recv(
          Gosh::encode_frame( at( body, 0 ) % 7, at( body, 1 ) % 3, at( body, 2 ) % 80, seq, payload ) );
        break;
      }

      case 2: { /* a whole datagram, cut into fragments as a real sender would (and maybe short of one) */
        const std::string datagram = body + std::string( at( body, 0 ) * 16, 'd' );
        const std::vector<std::string> fragments
          = Gosh::DatagramAssembly::split( datagram, link.limit - Gosh::FRAME_HEADER_LEN, at( body, 1 ) & 1 );
        for ( size_t i = 0; i < fragments.size(); i++ ) {
          if ( ( at( body, 1 ) & 2 ) && i == at( body, 2 ) % fragments.size() ) {
            seq++;
            continue;
          }
          forwarder.sidechannel_recv( Gosh::encode_frame(
            Gosh::TYPE_UDP_FRAGMENT, 0, at( body, 3 ) % 4, seq++, fragments[i] ) );
        }
        break;
      }

      case 3: { /* the target answers whoever last sent to it, and the forwarder relays the answer */
        struct sockaddr_in from;
        socklen_t len = sizeof( from );
        if ( recvfrom( target, buf, sizeof( buf ), 0, reinterpret_cast<struct sockaddr*>( &from ), &len ) >= 0 ) {
          const std::string reply( at( body, 0 ) * 40 + at( body, 1 ), 'r' );
          sendto( target, reply.data(), reply.size(), 0, reinterpret_cast<struct sockaddr*>( &from ), len );
        }
        const std::vector<int> fds = forwarder.fds();
        for ( size_t i = 0; i < fds.size(); i++ ) {
          forwarder.read_fd( fds[i] );
        }
        break;
      }
    }
    require( forwarder.flow_count() <= UDPForwarder::MAX_FLOWS );
    require( forwarder.partial_count() <= UDPForwarder::MAX_PARTIALS );
  }

  /* don't let one input's datagrams reach the next */
  while ( recv( target, buf, sizeof( buf ), 0 ) >= 0 ) {
  }
  return 0;
}
