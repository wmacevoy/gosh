/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).

    The algorithm is the fork's (mosh-1.4.0-pf.7, src/network/udpforward.cc:
    UDPForwarder's flows, partial datagrams and to_network), which is
    Copyright 2012 Keith Winstein and the fork's authors, under the same license.
*/

/*
 * conversations: the UDP forwards of one direction, as logic (REQ1/FWD-2, REQ6/SHAPE-5).
 *
 * A conversation is one application's exchange with one target through a forward. The
 * listening end names it by a number of its choosing when the application's first datagram
 * arrives; the other end opens a socket to the target for it when it first hears the number.
 * A conversation not used for two minutes is forgotten, and so is the quietest when there are
 * sixty-four and another begins.
 *
 * This is told what happened, with the time and what the session can do just now, and says
 * what to do through Out, which its caller supplies: the edge in the programs (udpforward.*),
 * a recorder in tests. It reads no clock and makes no system call.
 *
 *   Out::send( payload )                          put this on the side channel
 *   Out::open( handle, forward, id ) -> bool      a socket to this forward's target (the connecting end);
 *                                                 id is the conversation's number, for saying so
 *   Out::close( handle )                          and close it
 *   Out::to_target( handle, datagram )            send on that socket
 *   Out::to_application( forward, peer, datagram ) send from the forward's listener to this address
 *
 * What it does when an input is absent, late, wrong or lying:
 *   the session not ready: an application's datagram is dropped (UDP promises no more);
 *   a datagram too large for one payload: sent as fragments, with parity when asked or worth it;
 *     too large for 255 fragments: dropped, and counted;
 *   a fragment that never comes: the datagram is abandoned a second after its first fragment;
 *   a payload for a forward there is not, of another kind, or malformed: dropped;
 *   a payload seen before, or older than 1,024 behind the newest: dropped (the replay window);
 *   a reply for a conversation the listening end has forgotten: dropped.
 */

#ifndef GOSH_CONVERSATIONS_H
#define GOSH_CONVERSATIONS_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "src/forwarding/fragments.h"
#include "src/forwarding/frame.h"

namespace Gosh {

/* What the session can do just now, read by the edge each time. */
struct DatagramLink
{
  bool ready;         /* the other end forwards, and we are connected */
  size_t max_payload; /* the most one side-channel payload may be */
  double loss;        /* the share of datagrams the session is losing, 0 to 1 */
};

struct ConversationRules
{
  enum ParityMode
  {
    PARITY_AUTO, /* when the measured loss makes it worthwhile */
    PARITY_ALWAYS,
    PARITY_NEVER
  };

  /* Datagrams being put back together: how many at once, and for how long. */
  static constexpr size_t MAX_PARTIALS = 64;
  static constexpr uint64_t REASSEMBLY_TIMEOUT = 1000; /* ms */
  static constexpr size_t MAX_REMEMBERED = 256;

  static constexpr size_t MAX_FLOWS = 64;
  static constexpr uint64_t FLOW_IDLE_TIMEOUT = 120000; /* ms */
};

template<class Out>
class Conversations : public ConversationRules
{
private:
  struct Flow
  {
    uint8_t forward;
    uint16_t id;
    uint32_t handle;  /* connecting end: names our socket to the target */
    std::string peer; /* listening end: the address of the application that began it */
    uint64_t last_used;

    Flow() : forward( 0 ), id( 0 ), handle( 0 ), peer(), last_used( 0 ) {}
  };

  struct Partial
  {
    DatagramAssembly assembly;
    uint8_t forward;
    uint16_t flow_id;
    uint64_t started;

    Partial() : assembly(), forward( 0 ), flow_id( 0 ), started( 0 ) {}
  };

  bool listening;
  uint8_t data_type, fragment_type;
  size_t forwards;
  ParityMode parity_mode;

  std::vector<Flow> flows;
  uint16_t next_flow_id;
  uint32_t next_handle;

  uint64_t next_seq;
  ReplayWindow replay;

  /*
   * Fragments of a datagram are sent with consecutive sequence numbers, so the first one's
   * number (a fragment's own, less its index) names the datagram.
   */
  std::map<uint64_t, Partial> partials;
  std::deque<uint64_t> finished; /* recently rebuilt, so stragglers don't start them again */

  uint64_t dropped_oversize;
  uint64_t fragmented, with_parity, reassembled, recovered, abandoned;

  void remove_flow( size_t index, Out& out )
  {
    if ( !listening ) {
      out.close( flows[index].handle );
    }
    flows.erase( flows.begin() + index );
  }

  void expire_flows( uint64_t now, Out& out )
  {
    for ( size_t i = flows.size(); i-- > 0; ) {
      if ( now - flows[i].last_used > FLOW_IDLE_TIMEOUT ) {
        remove_flow( i, out );
      }
    }
  }

  /* The returned pointer is only good until the next change to the flow table. */
  Flow* new_flow( uint8_t forward, uint16_t id, uint64_t now, Out& out )
  {
    expire_flows( now, out );

    if ( flows.size() >= MAX_FLOWS ) {
      /* make room by forgetting the conversation that has been quiet longest */
      size_t oldest = 0;
      for ( size_t i = 1; i < flows.size(); i++ ) {
        if ( flows[i].last_used < flows[oldest].last_used ) {
          oldest = i;
        }
      }
      remove_flow( oldest, out );
    }

    Flow flow;
    flow.forward = forward;
    flow.id = id;
    flow.last_used = now;

    if ( !listening ) {
      flow.handle = next_handle++;
      if ( !out.open( flow.handle, forward, id ) ) {
        return NULL;
      }
    }

    flows.push_back( flow );
    return &flows.back();
  }

  void to_network( const Flow& flow, const char* data, size_t len, const DatagramLink& link, Out& out )
  {
    if ( !link.ready ) {
      return; /* peer doesn't (yet) support forwarding, or we're not connected */
    }

    const size_t limit = link.max_payload;
    const std::string datagram( data, len );

    if ( FRAME_HEADER_LEN + len <= limit ) {
      out.send( encode_frame( data_type, flow.forward, flow.id, next_seq++, datagram ) );
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
      out.send( encode_frame( fragment_type, flow.forward, flow.id, next_seq++, *i ) );
    }
    fragmented++;
    with_parity += parity ? 1 : 0;
  }

  void expire_partials( uint64_t now )
  {
    for ( typename std::map<uint64_t, Partial>::iterator i = partials.begin(); i != partials.end(); ) {
      if ( now - i->second.started > REASSEMBLY_TIMEOUT ) {
        partials.erase( i++ ); /* a fragment was lost, and with it the datagram */
        abandoned++;
      } else {
        i++;
      }
    }
  }

  void fragment_recv( uint8_t forward, uint16_t flow_id, uint64_t seq, const std::string& fragment, uint64_t now, Out& out )
  {
    unsigned int index;
    if ( !DatagramAssembly::index_of( fragment, index ) ) {
      return;
    }
    const uint64_t first_seq = seq - index;

    if ( std::find( finished.begin(), finished.end(), first_seq ) != finished.end() ) {
      return; /* already rebuilt without this one (typically, parity that wasn't needed) */
    }

    expire_partials( now );

    typename std::map<uint64_t, Partial>::iterator i = partials.find( first_seq );
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
      return;
    }

    std::string datagram;
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
        deliver( forward, flow_id, datagram, now, out );
        break;
      case DatagramAssembly::REJECTED:
        if ( i->second.assembly.empty() ) {
          partials.erase( i ); /* don't let junk hold a place */
        }
        break;
      case DatagramAssembly::INCOMPLETE:
        break;
    }
  }

  /* A whole datagram from the other end: pass it on. */
  void deliver( uint8_t forward, uint16_t flow_id, const std::string& datagram, uint64_t now, Out& out )
  {
    Flow* flow = NULL;
    for ( typename std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
      if ( i->forward == forward && i->id == flow_id ) {
        flow = &*i;
        break;
      }
    }

    if ( listening ) {
      if ( !flow ) {
        return; /* a reply to a conversation we have forgotten */
      }
      flow->last_used = now;
      out.to_application( forward, flow->peer, datagram );
    } else {
      if ( !flow ) {
        flow = new_flow( forward, flow_id, now, out );
        if ( !flow ) {
          return;
        }
      }
      flow->last_used = now;
      out.to_target( flow->handle, datagram );
    }
  }

public:
  /*
   * s_listening: this end has the listeners (the client, for -LU; the server, for -RU).
   * reverse: these are forwards the server listens for, which have frame types of their own.
   * s_forwards: how many forwards there are; both ends were given the same list.
   */
  Conversations( bool s_listening, bool reverse, size_t s_forwards )
    : listening( s_listening ), data_type( reverse ? TYPE_UDP_REVERSE_DATA : TYPE_UDP_DATA ),
      fragment_type( reverse ? TYPE_UDP_REVERSE_FRAGMENT : TYPE_UDP_FRAGMENT ), forwards( s_forwards ),
      parity_mode( PARITY_AUTO ), flows(), next_flow_id( 0 ), next_handle( 0 ), next_seq( 0 ), replay(), partials(),
      finished(), dropped_oversize( 0 ), fragmented( 0 ), with_parity( 0 ), reassembled( 0 ), recovered( 0 ),
      abandoned( 0 )
  {}

  /* When to add parity to the datagrams we fragment. (What we receive may have it regardless.) */
  void set_parity_mode( ParityMode s_mode ) { parity_mode = s_mode; }

  /* Listening end: a datagram came to this forward's listener from this address (any bytes that name it). */
  void from_application( uint8_t forward,
                         const std::string& peer,
                         const char* data,
                         size_t len,
                         uint64_t now,
                         const DatagramLink& link,
                         Out& out )
  {
    /* which conversation is this? */
    Flow* flow = NULL;
    for ( typename std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
      if ( i->forward == forward && i->peer == peer ) {
        flow = &*i;
        break;
      }
    }
    if ( !flow ) {
      /* choose an id that no live conversation on this forward is using */
      for ( typename std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); ) {
        if ( i->forward == forward && i->id == next_flow_id ) {
          next_flow_id++;
          i = flows.begin();
        } else {
          i++;
        }
      }
      flow = new_flow( forward, next_flow_id++, now, out );
      if ( !flow ) {
        return;
      }
      flow->peer = peer;
    }

    flow->last_used = now;
    to_network( *flow, data, len, link, out );
  }

  /* Connecting end: a datagram came from the target on this conversation's socket. */
  void from_target( uint32_t handle, const char* data, size_t len, uint64_t now, const DatagramLink& link, Out& out )
  {
    for ( typename std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
      if ( i->handle == handle ) {
        i->last_used = now;
        to_network( *i, data, len, link, out );
        return;
      }
    }
  }

  /* A payload of ours has arrived from the other end of the session. */
  void payload( const std::string& payload, uint64_t now, Out& out )
  {
    uint8_t type, forward;
    uint16_t flow_id;
    uint64_t seq;
    std::string data;

    if ( !decode_frame( payload, type, forward, flow_id, seq, data ) || forward >= forwards
         || ( type != data_type && type != fragment_type ) ) {
      return;
    }

    /* The session authenticates every datagram but does not stop an old
       one from being replayed, so that is up to us. */
    if ( !replay.accept( seq ) ) {
      return;
    }

    if ( type == fragment_type ) {
      fragment_recv( forward, flow_id, seq, data, now, out );
    } else {
      deliver( forward, flow_id, data, now, out );
    }
  }

  size_t flow_count( void ) const { return flows.size(); }
  uint64_t get_dropped_oversize( void ) const { return dropped_oversize; }

  /* Datagrams we sent in fragments; and of those received so: rebuilt, rebuilt using parity, given up on. */
  uint64_t get_fragmented( void ) const { return fragmented; }
  uint64_t get_with_parity( void ) const { return with_parity; }
  uint64_t get_reassembled( void ) const { return reassembled; }
  uint64_t get_recovered( void ) const { return recovered; }
  uint64_t get_abandoned( void ) const { return abandoned; }
  size_t partial_count( void ) const { return partials.size(); }
};

}

#endif
