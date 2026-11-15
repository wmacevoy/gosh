/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, passing: datagrams passed on between the two ends of a pair (REQ15/PATH-5; design v2,
 * 2.2). Logic: a datagram, where it came from and the time in, where to send it on out. No
 * socket, no clock. The statements are the fork's (mosh-1.4.0-pf.7, src/network/punch.cc,
 * meetup_serve), which did this between its calls to recvfrom and sendto.
 *
 * A datagram to be passed on is: the mark (8 bytes), a proof of the key (12), the name of a
 * pair of ends (16), and then what is passed, which this never looks at. It goes on, whole, to
 * whichever other address has lately sent a datagram naming the same pair.
 *
 * Absent: a pair with one end has nobody to pass to; the datagram is dropped. Late: an end not
 * heard for two minutes is not passed to; a pair neither of whose ends has been heard for two
 * minutes is forgotten. Wrong: too short, no mark, or a proof that is not good: dropped,
 * unanswered. More than 4,096 pairs: a new one is refused. Lying: a proof is believed if it is
 * good, whoever sends it and for whichever pair: see proof.h for what that does not show.
 */

#ifndef GOSH_MEETUP_PASSING_H
#define GOSH_MEETUP_PASSING_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "src/meetup/proof.h"

namespace Gosh {

class Passing
{
public:
  static constexpr size_t MAX_PAIRS = 4096;
  static constexpr uint64_t PAIR_IDLE = 120;   /* seconds */
  static constexpr uint64_t SWEEP_EVERY = 30;  /* seconds */

private:
  /* The pairs of ends we pass datagrams between: for each, the two addresses that have lately named it. */
  struct End
  {
    std::string address; /* as the caller names it: any bytes */
    uint64_t heard;      /* 0: never */

    End() : address(), heard( 0 ) {}
  };
  struct Pair
  {
    End ends[2];
  };
  std::map<std::string, Pair> pairs;
  std::set<std::string> proofs; /* ones already found good, to save working them out again */
  uint64_t swept;

public:
  Passing() : pairs(), proofs(), swept( 0 ) {}

  /*
   * A datagram has arrived.
   *   data, n   the datagram
   *   from      where it came from, as any bytes that name the address
   *   now       the time, in seconds (never 0)
   *   good      called with twelve bytes: whether they show the key (may be costly; asked once for each proof)
   * True if it is to be sent on, whole, to `to`. False for everything else: not for passing
   * on (see is_for_passing), not good, or nobody to pass it to.
   */
  template<class Good>
  bool datagram( const char* data, size_t n, const std::string& from, uint64_t now, Good good, std::string& to )
  {
    if ( !is_for_passing( data, n ) ) {
      return false;
    }
    const std::string proof( data + 8, 12 );
    if ( proofs.find( proof ) == proofs.end() ) {
      if ( !good( proof.data() ) ) {
        return false;
      }
      if ( proofs.size() >= MAX_PAIRS ) {
        proofs.clear();
      }
      proofs.insert( proof );
    }
    if ( now - swept > SWEEP_EVERY ) {
      swept = now;
      for ( std::map<std::string, Pair>::iterator i = pairs.begin(); i != pairs.end(); ) {
        if ( now - std::max( i->second.ends[0].heard, i->second.ends[1].heard ) > PAIR_IDLE ) {
          pairs.erase( i++ );
        } else {
          i++;
        }
      }
    }
    const std::string name( data + 20, 16 );
    std::map<std::string, Pair>::iterator found = pairs.find( name );
    if ( found == pairs.end() ) {
      if ( pairs.size() >= MAX_PAIRS ) {
        return false;
      }
      found = pairs.insert( std::make_pair( name, Pair() ) ).first;
    }
    Pair& pair = found->second;
    /* Which end is this?  One we know; or a new one, in place of whichever has been quiet longest. */
    int which = -1;
    for ( int i = 0; i < 2; i++ ) {
      if ( pair.ends[i].heard && pair.ends[i].address == from ) {
        which = i;
      }
    }
    if ( which < 0 ) {
      which = pair.ends[0].heard <= pair.ends[1].heard ? 0 : 1;
      pair.ends[which].address = from;
    }
    pair.ends[which].heard = now;
    const End& other = pair.ends[1 - which];
    if ( other.heard && now - other.heard <= PAIR_IDLE ) {
      to = other.address;
      return true;
    }
    return false;
  }

  /* Whether a datagram is one to be passed on: long enough for the header, and beginning with the mark. */
  static bool is_for_passing( const char* data, size_t n )
  {
    return n >= Network::MEETUP_HEADER && memcmp( data, Network::MEETUP_MARK, sizeof( Network::MEETUP_MARK ) ) == 0;
  }

  size_t pair_count( void ) const { return pairs.size(); }
};

}

#endif
