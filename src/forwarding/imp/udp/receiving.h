/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * forwarding, UDP, receiving: what has arrived from the other end, made into whole datagrams
 * (REQ1/FWD-2). Logic, given the time. The statements are the fork's (UDPForwarder's replay
 * window, expire_partials and fragment_recv).
 *
 * A payload seen before, or older than 1,024 behind the newest: refused (the session proves a
 * datagram is its own, and does not stop an old one being sent again). A fragment that never
 * comes: its datagram is abandoned a second after its first fragment. More than 64 datagrams
 * in pieces at once: the oldest is abandoned. A fragment for a datagram already rebuilt (parity
 * that was not needed) is ignored, for the last 256 rebuilt. A fragment that says it is of
 * another conversation's datagram, or makes no sense: refused, and holds no place.
 */

#ifndef GOSH_UDP_RECEIVING_H
#define GOSH_UDP_RECEIVING_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>

#include "src/forwarding/imp/frame.h"
#include "src/forwarding/imp/udp/fragments.h"
#include "src/forwarding/rules.h"

namespace Gosh::Forwarding::Imp {

class Receiving : public ConversationRules
{
private:
  struct Partial
  {
    DatagramAssembly assembly;
    uint8_t forward;
    uint16_t flow_id;
    uint64_t started;

    Partial() : assembly(), forward( 0 ), flow_id( 0 ), started( 0 ) {}
  };

  ReplayWindow replay;

  /*
   * Fragments of a datagram are sent with consecutive sequence numbers, so the first one's
   * number (a fragment's own, less its index) names the datagram.
   */
  std::map<uint64_t, Partial> partials;
  std::deque<uint64_t> finished; /* recently rebuilt, so stragglers don't start them again */

  uint64_t reassembled, recovered, abandoned;

  void expire_partials( uint64_t now )
  {
    for ( std::map<uint64_t, Partial>::iterator i = partials.begin(); i != partials.end(); ) {
      if ( now - i->second.started > REASSEMBLY_TIMEOUT ) {
        partials.erase( i++ ); /* a fragment was lost, and with it the datagram */
        abandoned++;
      } else {
        i++;
      }
    }
  }

public:
  Receiving() : replay(), partials(), finished(), reassembled( 0 ), recovered( 0 ), abandoned( 0 ) {}

  /* Whether a payload with this sequence number is new: true once for each, unless too old to tell. */
  bool fresh( uint64_t seq ) { return replay.accept( seq ); }

  /*
   * A fragment has arrived (and was not a replay). True if it completes its datagram, which is
   * then in `datagram`.
   */
  bool fragment( uint8_t forward, uint16_t flow_id, uint64_t seq, const std::string& fragment, uint64_t now, std::string& datagram )
  {
    unsigned int index;
    if ( !DatagramAssembly::index_of( fragment, index ) ) {
      return false;
    }
    const uint64_t first_seq = seq - index;

    if ( std::find( finished.begin(), finished.end(), first_seq ) != finished.end() ) {
      return false; /* already rebuilt without this one (typically, parity that wasn't needed) */
    }

    expire_partials( now );

    std::map<uint64_t, Partial>::iterator i = partials.find( first_seq );
    if ( i == partials.end() ) {
      if ( partials.size() >= MAX_PARTIALS ) {
        partials.erase( partials.begin() ); /* the oldest */
        abandoned++;
      }
      Partial fresh;
      fresh.forward = forward;
      fresh.flow_id = flow_id;
      fresh.started = now;
      i = partials.insert( std::make_pair( first_seq, fresh ) ).first;
    } else if ( i->second.forward != forward || i->second.flow_id != flow_id ) {
      return false;
    }

    bool used_parity = false;
    switch ( i->second.assembly.add( fragment, datagram, used_parity ) ) {
      case DatagramAssembly::COMPLETE:
        partials.erase( i );
        finished.push_back( first_seq );
        if ( finished.size() > MAX_REMEMBERED ) {
          finished.pop_front();
        }
        reassembled++;
        recovered += used_parity ? 1 : 0;
        return true;
      case DatagramAssembly::REJECTED:
        if ( i->second.assembly.empty() ) {
          partials.erase( i ); /* don't let junk hold a place */
        }
        break;
      case DatagramAssembly::INCOMPLETE:
        break;
    }
    return false;
  }

  /* Datagrams received in fragments: rebuilt, rebuilt using parity, given up on; and how many are in pieces now. */
  uint64_t get_reassembled( void ) const { return reassembled; }
  uint64_t get_recovered( void ) const { return recovered; }
  uint64_t get_abandoned( void ) const { return abandoned; }
  size_t partial_count( void ) const { return partials.size(); }
};

}

#endif
