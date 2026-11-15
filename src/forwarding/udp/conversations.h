/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The algorithm is the fork's (mosh-1.4.0-pf.7, src/network/udpforward.cc: UDPForwarder's flows). */

/*
 * conversations: the UDP forwards of one direction, as logic (REQ1/FWD-2, REQ6/SHAPE-5).
 *
 * A conversation is one application's exchange with one target through a forward. The
 * listening end names it by a number of its choosing when the application's first datagram
 * arrives; the other end opens a socket to the target for it when it first hears the number.
 * A conversation not used for two minutes is forgotten, and so is the quietest when there are
 * sixty-four and another begins.
 *
 * This is the table: which conversation a datagram is of. A datagram on its way out goes
 * through sending.h (one payload, or fragments); what arrives goes through receiving.h (the
 * replay window; datagrams in pieces) and is given here whole.
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
 * What it does when an input is absent, late, wrong or lying (for what sending.h and
 * receiving.h do with theirs, see them):
 *   a payload for a forward there is not, of another kind, or malformed: dropped;
 *   a reply for a conversation the listening end has forgotten: dropped;
 *   no socket to be had for a new conversation: its datagram is dropped, and no conversation kept.
 */

#ifndef GOSH_CONVERSATIONS_H
#define GOSH_CONVERSATIONS_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "src/forwarding/frame.h"
#include "src/forwarding/udp/receiving.h"
#include "src/forwarding/udp/rules.h"
#include "src/forwarding/udp/sending.h"

namespace Gosh {

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

  bool listening;
  uint8_t data_type, fragment_type;
  size_t forwards;

  std::vector<Flow> flows;
  uint16_t next_flow_id;
  uint32_t next_handle;

  Sending sending;     /* a datagram on its way out: one payload or fragments (sending.h) */
  Receiving receiving; /* what arrives, made into whole datagrams (receiving.h) */

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
      /* a handle no live conversation has (the counter comes round, after four thousand million) */
      for ( typename std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); ) {
        if ( i->handle == next_handle ) {
          next_handle++;
          i = flows.begin();
        } else {
          i++;
        }
      }
      flow.handle = next_handle++;
      if ( !out.open( flow.handle, forward, id ) ) {
        return NULL;
      }
    }

    flows.push_back( flow );
    return &flows.back();
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
      flows(), next_flow_id( 0 ), next_handle( 0 ), sending( reverse ), receiving()
  {}

  /* When to add parity to the datagrams we fragment. (What we receive may have it regardless.) */
  void set_parity_mode( ParityMode s_mode ) { sending.set_parity_mode( s_mode ); }

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
    sending.send( flow->forward, flow->id, data, len, link, out );
  }

  /* Connecting end: a datagram came from the target on this conversation's socket. */
  void from_target( uint32_t handle, const char* data, size_t len, uint64_t now, const DatagramLink& link, Out& out )
  {
    for ( typename std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
      if ( i->handle == handle ) {
        i->last_used = now;
        sending.send( i->forward, i->id, data, len, link, out );
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
    if ( !receiving.fresh( seq ) ) {
      return;
    }

    if ( type == fragment_type ) {
      std::string datagram;
      if ( receiving.fragment( forward, flow_id, seq, data, now, datagram ) ) {
        deliver( forward, flow_id, datagram, now, out );
      }
    } else {
      deliver( forward, flow_id, data, now, out );
    }
  }

  size_t flow_count( void ) const { return flows.size(); }
  uint64_t get_dropped_oversize( void ) const { return sending.get_dropped_oversize(); }

  /* Datagrams we sent in fragments; and of those received so: rebuilt, rebuilt using parity, given up on. */
  uint64_t get_fragmented( void ) const { return sending.get_fragmented(); }
  uint64_t get_with_parity( void ) const { return sending.get_with_parity(); }
  uint64_t get_reassembled( void ) const { return receiving.get_reassembled(); }
  uint64_t get_recovered( void ) const { return receiving.get_recovered(); }
  uint64_t get_abandoned( void ) const { return receiving.get_abandoned(); }
  size_t partial_count( void ) const { return receiving.partial_count(); }
};

}

#endif
