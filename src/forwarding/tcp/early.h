/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * forwarding, TCP, receiving: segments that arrived before their turn, kept until it comes; and
 * what to tell the sender we hold (selective acknowledgment, RFC 2018; requirements v14). Logic.
 *
 * rcv_nxt is the next byte wanted in order. `limit` is the receive buffer: nothing further
 * ahead than that is kept, nor more than that much in all. Distances are from rcv_nxt, which is
 * how sequence numbers that wrap are compared: one more than `limit` "ahead" is behind.
 */

#ifndef GOSH_TCP_EARLY_H
#define GOSH_TCP_EARLY_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/forwarding/tcp/options.h"

namespace Gosh {

class Early
{
private:
  std::map<uint32_t, std::string> segments; /* by sequence number */
  size_t held;                              /* how many bytes in all */
  bool latest_valid;
  uint32_t latest; /* where the one that arrived last begins (it is reported first) */

public:
  Early() : segments(), held( 0 ), latest_valid( false ), latest( 0 ) {}

  bool empty( void ) const { return segments.empty(); }

  void clear( void )
  {
    segments.clear();
    held = 0;
  }

  /* Keeps a segment that is ahead of rcv_nxt, if it is not absurdly far ahead and there is room. */
  void keep( uint32_t seq, const std::string& payload, uint32_t rcv_nxt, size_t limit )
  {
    const uint32_t ahead = seq - rcv_nxt;
    if ( !payload.empty() && ahead <= limit && held + payload.size() <= limit
         && segments.insert( std::make_pair( seq, payload ) ).second ) {
      held += payload.size();
      latest = seq;
      latest_valid = true;
    }
  }

  /*
   * rcv_nxt has moved: takes out the segment that continues from it, if there is one. Any
   * that rcv_nxt has passed altogether are thrown away on the way.
   */
  bool take_next( uint32_t rcv_nxt, size_t limit, uint32_t& seq, std::string& payload )
  {
    for ( std::map<uint32_t, std::string>::iterator i = segments.begin(); i != segments.end(); ) {
      const uint32_t behind = rcv_nxt - i->first;
      if ( behind > limit ) {
        i++; /* still ahead */
        continue;
      }
      payload = i->second;
      seq = i->first;
      held -= payload.size();
      segments.erase( i );
      if ( behind <= payload.size() ) {
        return true;
      }
      i = segments.begin(); /* that one was stale; start over */
    }
    return false;
  }

  /*
   * The selective acknowledgment option for an ACK, saying which ranges beyond rcv_nxt are
   * held. The range the latest arrival fell in comes first, as RFC 2018 asks, so that every
   * arrival is reported at least once; then the others, nearest first; `room` of them at most,
   * and four at most. Empty if nothing is held.
   */
  std::string blocks( uint32_t rcv_nxt, size_t limit, size_t room ) const
  {
    /* what is held, as distances beyond rcv_nxt, joined where segments touch or overlap */
    std::vector<std::pair<uint32_t, uint32_t> > each;
    for ( std::map<uint32_t, std::string>::const_iterator i = segments.begin(); i != segments.end(); i++ ) {
      const uint32_t from = i->first - rcv_nxt;
      if ( from > limit ) {
        continue; /* behind us by now (it will be thrown away), not ahead */
      }
      each.push_back( std::make_pair( from, from + static_cast<uint32_t>( i->second.size() ) ) );
    }
    std::sort( each.begin(), each.end() );
    std::vector<std::pair<uint32_t, uint32_t> > ranges;
    for ( size_t i = 0; i < each.size(); i++ ) {
      if ( !ranges.empty() && each[i].first <= ranges.back().second ) {
        ranges.back().second = std::max( ranges.back().second, each[i].second );
      } else {
        ranges.push_back( each[i] );
      }
    }
    if ( ranges.empty() ) {
      return std::string();
    }

    size_t first = ranges.size(); /* the range the latest arrival is in, if it is still held */
    if ( latest_valid ) {
      const uint32_t where = latest - rcv_nxt;
      for ( size_t i = 0; i < ranges.size(); i++ ) {
        if ( ranges[i].first <= where && where < ranges[i].second ) {
          first = i;
        }
      }
    }

    const size_t count = std::min<size_t>( std::min<size_t>( ranges.size(), 4 ), room );
    if ( count == 0 ) {
      return std::string();
    }
    std::string option;
    const char head[4] = { 1, 1, 5, static_cast<char>( 2 + 8 * count ) };
    option.assign( head, sizeof( head ) );
    size_t written = 0;
    if ( first < ranges.size() ) {
      SegmentOptions::put32( option, rcv_nxt + ranges[first].first );
      SegmentOptions::put32( option, rcv_nxt + ranges[first].second );
      written++;
    }
    for ( size_t i = 0; i < ranges.size() && written < count; i++ ) {
      if ( i != first ) {
        SegmentOptions::put32( option, rcv_nxt + ranges[i].first );
        SegmentOptions::put32( option, rcv_nxt + ranges[i].second );
        written++;
      }
    }
    return option;
  }
};

}

#endif
