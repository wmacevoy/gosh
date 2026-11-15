/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <cstring>

#include "src/crypto/byteorder.h"
#include "src/forwarding/frame.h"
#include "src/network/sidechannel.h"

using namespace Gosh;

bool ReplayWindow::accept( uint64_t seq )
{
  if ( !any ) {
    any = true;
    highest = seq;
    seen[seq % WINDOW] = true;
    return true;
  }

  if ( seq > highest ) {
    /* slide forward, forgetting whatever falls off the back */
    if ( seq - highest >= WINDOW ) {
      seen.assign( WINDOW, false );
    } else {
      for ( uint64_t i = highest + 1; i < seq; i++ ) {
        seen[i % WINDOW] = false;
      }
    }
    highest = seq;
    seen[seq % WINDOW] = true;
    return true;
  }

  if ( highest - seq >= WINDOW ) {
    return false; /* too old to know whether we have seen it */
  }

  if ( seen[seq % WINDOW] ) {
    return false;
  }
  seen[seq % WINDOW] = true;
  return true;
}

std::string Gosh::encode_frame( uint8_t type,
                                        uint8_t forward,
                                        uint16_t flow_id,
                                        uint64_t seq,
                                        const std::string& data )
{
  std::string ret;
  ret.reserve( FRAME_HEADER_LEN + data.size() );

  ret.push_back( static_cast<char>( Network::SIDECHANNEL_MAGIC ) );
  ret.push_back( static_cast<char>( type ) );
  ret.push_back( static_cast<char>( forward ) );

  uint16_t flow_net = htobe16( flow_id );
  ret.append( reinterpret_cast<const char*>( &flow_net ), sizeof( flow_net ) );

  uint64_t seq_net = htobe64( seq );
  ret.append( reinterpret_cast<const char*>( &seq_net ), sizeof( seq_net ) );

  ret.append( data );
  return ret;
}

bool Gosh::decode_frame( const std::string& payload,
                                 uint8_t& type,
                                 uint8_t& forward,
                                 uint16_t& flow_id,
                                 uint64_t& seq,
                                 std::string& data )
{
  if ( payload.size() < FRAME_HEADER_LEN || static_cast<uint8_t>( payload[0] ) != Network::SIDECHANNEL_MAGIC ) {
    return false;
  }

  type = static_cast<uint8_t>( payload[1] );
  forward = static_cast<uint8_t>( payload[2] );

  uint16_t flow_net;
  memcpy( &flow_net, payload.data() + 3, sizeof( flow_net ) );
  flow_id = be16toh( flow_net );

  uint64_t seq_net;
  memcpy( &seq_net, payload.data() + 5, sizeof( seq_net ) );
  seq = be64toh( seq_net );

  data.assign( payload, FRAME_HEADER_LEN, std::string::npos );
  return true;
}
