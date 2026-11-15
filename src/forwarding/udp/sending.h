/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * forwarding, UDP, sending: a datagram on its way to the other end, as one payload or as
 * fragments (REQ1/FWD-2). Logic: it says what to put on the side channel through Out::send, and
 * reads no clock and no socket. The statements are the fork's (UDPForwarder::to_network).
 *
 * The session not ready: the datagram is dropped (UDP promises no more). Too large for one
 * payload: fragments, numbered one after another, which the other end relies on. Too large
 * for 255 fragments, parity among them: dropped, and counted.
 */

#ifndef GOSH_UDP_SENDING_H
#define GOSH_UDP_SENDING_H

#include <cstdint>
#include <string>
#include <vector>

#include "src/forwarding/frame.h"
#include "src/forwarding/udp/fragments.h"
#include "src/forwarding/udp/rules.h"

namespace Gosh {

class Sending : public ConversationRules
{
private:
  uint8_t data_type, fragment_type;
  ParityMode parity_mode;
  uint64_t next_seq;
  uint64_t dropped_oversize, fragmented, with_parity;

public:
  /* reverse: these are forwards the server listens for, which have frame types of their own. */
  explicit Sending( bool reverse )
    : data_type( reverse ? TYPE_UDP_REVERSE_DATA : TYPE_UDP_DATA ),
      fragment_type( reverse ? TYPE_UDP_REVERSE_FRAGMENT : TYPE_UDP_FRAGMENT ), parity_mode( PARITY_AUTO ), next_seq( 0 ),
      dropped_oversize( 0 ), fragmented( 0 ), with_parity( 0 )
  {}

  /* When to add parity to the datagrams we fragment. */
  void set_parity_mode( ParityMode s_mode ) { parity_mode = s_mode; }

  /*
   * Sends one datagram of the conversation numbered `id` on this forward: as one payload if it
   * fits, or as fragments, with parity when asked or when it is worth it.
   */
  template<class Out>
  void send( uint8_t forward, uint16_t id, const char* data, size_t len, const DatagramLink& link, Out& out )
  {
    if ( !link.ready ) {
      return; /* peer doesn't (yet) support forwarding, or we're not connected */
    }

    const size_t limit = link.max_payload;
    const std::string datagram( data, len );

    if ( FRAME_HEADER_LEN + len <= limit ) {
      out.send( encode_frame( data_type, forward, id, next_seq++, datagram ) );
      return;
    }

    /* Too big to go in one piece.  Is it worth protecting the pieces? */
    std::vector<std::string> fragments;
    bool parity = false;
    if ( limit > FRAME_HEADER_LEN ) {
      const unsigned int count = DatagramAssembly::piece_count( len, limit - FRAME_HEADER_LEN );
      parity = parity_mode == PARITY_ALWAYS || ( parity_mode == PARITY_AUTO && parity_worthwhile( link.loss, count ) );
      fragments = DatagramAssembly::split( datagram, limit - FRAME_HEADER_LEN, parity );
    }
    if ( fragments.empty() ) {
      dropped_oversize++;
      return;
    }

    /* The receiver relies on these sequence numbers being consecutive. */
    for ( std::vector<std::string>::const_iterator i = fragments.begin(); i != fragments.end(); i++ ) {
      out.send( encode_frame( fragment_type, forward, id, next_seq++, *i ) );
    }
    fragmented++;
    with_parity += parity ? 1 : 0;
  }

  uint64_t get_dropped_oversize( void ) const { return dropped_oversize; }
  uint64_t get_fragmented( void ) const { return fragmented; }
  uint64_t get_with_parity( void ) const { return with_parity; }
};

}

#endif
