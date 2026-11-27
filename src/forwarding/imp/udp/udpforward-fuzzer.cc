/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/FWD-2: fuzzes the UDP forwarder (udpforward.h, around conversations.h's logic) as the
 * connecting end sees it: everything that can arrive on the side channel (frames, fragments,
 * replays, junk), and replies of every size from the forwarding target, which it must fragment
 * to send back.
 *
 * It uses real sockets on the loopback interface, since the forwarder does. What reaches the
 * target is held to REQ1/FWD-2 (review of item 21, 2026-11-16; before, nothing read it): each
 * datagram the harness sends is marked with where it came from, and a mark arrives at most once
 * (a frame's number brings at most one datagram: the replay check), carrying what was sent with it
 * first. What arrives after the junk of kind 0 is not judged: the harness cannot know what it says.
 *
 * (gosh) The fork's src/fuzz/udp_forward_fuzzer.cc (mosh-1.4.0-pf.7); the frame's functions and
 * numbers are frame.h's now, and reassembly fragments.h's. Its corpus is the fork's.
 */

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "src/forwarding/imp/frame.h"
#include "src/forwarding/imp/fuzz.h"
#include "src/forwarding/imp/udp/fragments.h"
#include "src/forwarding/imp/udp/udpforward.h"
#include "src/seam/fuzz-replay.h"
#include "src/util/timestamp.h"

using namespace Network;
using namespace Gosh::Forwarding;
using namespace Gosh::Forwarding::Imp;
using namespace ForwardFuzz;

namespace {

class Link : public Network::SideChannelLink
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
    require( payload.size() <= limit && payload.size() >= Gosh::Forwarding::Imp::FRAME_HEADER_LEN );
    require( static_cast<uint8_t>( payload[0] ) == SIDECHANNEL_MAGIC );
    if ( sent.size() < 64 ) {
      sent.push_back( payload );
    }
  }
};

int target = -1;
uint16_t target_port;
struct sockaddr_in target_addr;

/* A mark at the head of each datagram the harness sends: which frame's number, or which whole datagram. */
std::string mark( char kind, uint64_t n )
{
  char m[24];
  snprintf( m, sizeof( m ), "%c%016llx|", kind, static_cast<unsigned long long>( n ) );
  return m;
}

}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  if ( target < 0 ) {
    target = loopback_socket( SOCK_DGRAM, target_port, target_addr );
    set_nonblocking( target );
  }

  Input in( data, size );
  Util::freeze_timestamp();

  Link link;
  link.limit = Gosh::Forwarding::Imp::FRAME_HEADER_LEN + Gosh::Forwarding::Imp::DatagramAssembly::FRAGMENT_HEADER_LEN + 8 + in.byte() * 5;
  link.loss = in.byte() / 255.0;
  const unsigned int mode = in.byte() % 3;

  char spec[64];
  snprintf( spec, sizeof( spec ), "1:127.0.0.1:%d", target_port );
  UDPForwarder forwarder( &link, UDPForwarder::CONNECT, std::vector<std::string>( 2, spec ) );
  forwarder.set_parity_mode( mode == 0 ? UDPForwarder::PARITY_AUTO
                                       : mode == 1 ? UDPForwarder::PARITY_ALWAYS : UDPForwarder::PARITY_NEVER );

  uint64_t seq = 1000;
  uint64_t highest = seq; /* the highest frame number used so far */
  char buf[65536];

  /* What may reach the target, and what has: by mark. */
  std::map<uint64_t, std::string> first_sent;   /* a frame's number: the datagram first sent whole with it */
  std::set<uint64_t> numbers_arrived;          /* frame numbers whose datagram has arrived */
  std::map<uint64_t, std::string> whole_sent;   /* kind 2's datagrams, by record */
  std::set<uint64_t> wholes_arrived;
  uint64_t record = 0;
  bool mixed = false; /* a kind-1 frame of a fragment's type was sent: wholes may be made of anyone's pieces */
  struct sockaddr_in last_from;
  socklen_t last_len = 0;
  /* Everything at the target now: held to the marks, unless what came before it was junk. */
  auto arrived = [&]( bool judge ) {
    for ( ;; ) {
      struct sockaddr_in from;
      socklen_t len = sizeof( from );
      const ssize_t n = recvfrom( target, buf, sizeof( buf ), 0, reinterpret_cast<struct sockaddr*>( &from ), &len );
      if ( n < 0 ) {
        return;
      }
      last_from = from;
      last_len = len;
      if ( !judge ) {
        continue;
      }
      const std::string got( buf, n );
      const uint64_t which = got.size() >= 18 ? strtoull( got.substr( 1, 16 ).c_str(), NULL, 16 ) : 0;
      if ( got[0] == 'S' && first_sent.count( which ) && got == first_sent[which] ) {
        require( numbers_arrived.insert( which ).second ); /* one frame number, one datagram */
      } else if ( got[0] == 'W' && whole_sent.count( which ) && got == whole_sent[which] ) {
        require( wholes_arrived.insert( which ).second );
      } else {
        /* none of the harness's: only if pieces of its own were sent that a whole could be made of */
        require( mixed );
      }
    }
  };

  unsigned int kind;
  std::string body;
  while ( in.record( kind, body ) ) {
    record++;
    switch ( kind ) {
      case 0: /* anything at all */
        forwarder.sidechannel_recv( body );
        arrived( false );
        break;

      case 1: { /* a frame that at least looks like one */
        seq += static_cast<int8_t>( at( body, 3 ) ); /* (it may go back: frame numbers used again test the replay check) */
        highest = std::max( highest, seq );
        const uint8_t type = at( body, 0 ) % 7, forward = at( body, 1 ) % 3;
        const std::string payload = mark( 'S', seq ) + body.substr( body.size() < 4 ? body.size() : 4 );
        /* a whole datagram for a forward there is: what its number may bring, if it is the first so */
        if ( type == Gosh::Forwarding::Imp::TYPE_UDP_DATA && forward < 2 && !first_sent.count( seq ) ) {
          first_sent[seq] = payload;
        }
        mixed = mixed || type == Gosh::Forwarding::Imp::TYPE_UDP_FRAGMENT || type == Gosh::Forwarding::Imp::TYPE_UDP_REVERSE_DATA
                || type == Gosh::Forwarding::Imp::TYPE_UDP_REVERSE_FRAGMENT;
        forwarder.sidechannel_recv( Gosh::Forwarding::Imp::encode_frame( type, forward, at( body, 2 ) % 80, seq, payload ) );
        break;
      }

      case 2: { /* a whole datagram, cut into fragments as a real sender would (and maybe short of one) */
        /* At numbers never used before, as a real sender's are (they only rise, and are sealed): at a number
           another datagram had left unused, a fragment would join that one's pieces, which only the other
           end, holding the key, could cause, and it may send any datagram it likes (found by libFuzzer,
           2026-11-16: the harness, not the forwarder). */
        seq = highest + 1;
        const std::string datagram = mark( 'W', record ) + body + std::string( at( body, 0 ) * 16, 'd' );
        whole_sent[record] = datagram;
        const std::vector<std::string> fragments
          = Gosh::Forwarding::Imp::DatagramAssembly::split( datagram, link.limit - Gosh::Forwarding::Imp::FRAME_HEADER_LEN, at( body, 1 ) & 1 );
        for ( size_t i = 0; i < fragments.size(); i++ ) {
          if ( ( at( body, 1 ) & 2 ) && i == at( body, 2 ) % fragments.size() ) {
            seq++;
            continue;
          }
          forwarder.sidechannel_recv( Gosh::Forwarding::Imp::encode_frame(
            Gosh::Forwarding::Imp::TYPE_UDP_FRAGMENT, 0, at( body, 3 ) % 4, seq++, fragments[i] ) );
        }
        highest = std::max( highest, seq );
        break;
      }

      case 3: { /* the target answers whoever last sent to it, and the forwarder relays the answer */
        if ( last_len > 0 ) {
          const std::string reply( at( body, 0 ) * 40 + at( body, 1 ), 'r' );
          sendto( target, reply.data(), reply.size(), 0, reinterpret_cast<struct sockaddr*>( &last_from ), last_len );
        }
        const std::vector<int> fds = forwarder.fds();
        for ( size_t i = 0; i < fds.size(); i++ ) {
          forwarder.read_fd( fds[i] );
        }
        break;
      }
    }
    if ( kind != 0 ) {
      arrived( true );
    }
    require( forwarder.flow_count() <= UDPForwarder::MAX_FLOWS );
    require( forwarder.partial_count() <= UDPForwarder::MAX_PARTIALS );
  }

  /* don't let one input's datagrams reach the next */
  while ( recv( target, buf, sizeof( buf ), 0 ) >= 0 ) {
  }
  return 0;
}
