/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * forwarding, TCP, sending: what the other end says it holds beyond what it has acknowledged
 * (selective acknowledgment, RFC 2018; requirements v14). Logic.
 *
 * The ranges are kept as sequence numbers [first, second), in order of distance from snd_una
 * (the oldest byte not acknowledged), apart from each other. Distances are from snd_una, which
 * is how sequence numbers that wrap are compared.
 *
 * A range is believed only if it is about data we have sent and begins beyond snd_una: an end
 * that held from snd_una on would have acknowledged it. For the same reason a range that
 * snd_una reaches and stops inside was never held, and is forgotten (and so sent again).
 */

#ifndef GOSH_TCP_HELD_H
#define GOSH_TCP_HELD_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "src/forwarding/imp/tcp/options.h"

namespace Gosh::Forwarding::Imp {

class Held
{
private:
  std::vector<std::pair<uint32_t, uint32_t> > ranges;

public:
  Held() : ranges() {}

  bool empty( void ) const { return ranges.empty(); }

  /* Adds [from, to), as distances beyond snd_una. */
  void add( uint32_t snd_una, uint32_t from, uint32_t to )
  {
    std::vector<std::pair<uint32_t, uint32_t> > merged;
    bool placed = false;
    for ( size_t i = 0; i < ranges.size(); i++ ) {
      const uint32_t first = ranges[i].first - snd_una, second = ranges[i].second - snd_una;
      if ( second < from ) {
        merged.push_back( ranges[i] ); /* wholly before */
      } else if ( to < first ) {
        if ( !placed ) {
          merged.push_back( std::make_pair( snd_una + from, snd_una + to ) );
          placed = true;
        }
        merged.push_back( ranges[i] ); /* wholly after */
      } else {
        from = std::min( from, first ); /* touching or overlapping: it becomes part of the new one */
        to = std::max( to, second );
      }
    }
    if ( !placed ) {
      merged.push_back( std::make_pair( snd_una + from, snd_una + to ) );
    }
    ranges.swap( merged );
  }

  /*
   * Takes note of the ranges an acknowledgment's options say are held (kind 5).
   * outstanding: how many bytes of data from snd_una on have been sent (not a FIN, which takes
   * a sequence number and is no bytes of ours).
   */
  void note( const std::string& options, uint32_t snd_una, uint32_t outstanding )
  {
    SegmentOptions::walk( options, [&]( uint8_t kind, size_t at, size_t length ) {
      if ( kind == 5 && length % 8 == 0 ) {
        for ( size_t k = at; k + 8 <= at + length; k += 8 ) {
          const uint32_t from = SegmentOptions::get32( options, k ) - snd_una;
          const uint32_t to = SegmentOptions::get32( options, k + 4 ) - snd_una;
          /* (Anything else is not about what we have sent; and what begins at snd_una it would have
             acknowledged, not said it holds.) */
          if ( 0 < from && from < to && to <= outstanding ) {
            add( snd_una, from, to );
          }
        }
      }
      return true;
    } );
  }


  /* snd_una has moved: forget every range that begins at or before it. */
  void forget( uint32_t snd_una )
  {
    std::vector<std::pair<uint32_t, uint32_t> > kept;
    for ( size_t i = 0; i < ranges.size(); i++ ) {
      if ( static_cast<int32_t>( ranges[i].first - snd_una ) > 0 ) {
        kept.push_back( ranges[i] );
      }
    }
    ranges.swap( kept );
  }

  /* How many of the bytes from snd_una up to this distance are held. */
  uint32_t below( uint32_t snd_una, uint32_t distance ) const
  {
    uint32_t held = 0;
    for ( size_t i = 0; i < ranges.size(); i++ ) {
      const uint32_t first = ranges[i].first - snd_una, second = ranges[i].second - snd_una;
      if ( first >= distance ) {
        break;
      }
      held += std::min( second, distance ) - first;
    }
    return held;
  }

  /*
   * Sending again from an earlier point: if snd_nxt is at something held, moves it past.
   * Returns how far from there to the next thing held (so as not to send into it).
   */
  uint32_t pass_over( uint32_t snd_una, uint32_t& snd_nxt ) const
  {
    uint32_t at = snd_nxt - snd_una;
    uint32_t until = 0xFFFFFFFF;
    for ( size_t i = 0; i < ranges.size(); i++ ) {
      const uint32_t first = ranges[i].first - snd_una, second = ranges[i].second - snd_una;
      if ( first <= at && at < second ) {
        at = second;
      } else if ( at < first ) {
        until = first - at;
        break;
      }
    }
    snd_nxt = snd_una + at;
    return until;
  }

  /*
   * The next gap, from rexmit_nxt on, that lies below something held: where it begins and where
   * it ends, as distances beyond snd_una. False if there is none: what is beyond the last thing
   * held may simply not have arrived yet.
   */
  bool next_gap( uint32_t snd_una, uint32_t rexmit_nxt, uint32_t& at, uint32_t& gap_end ) const
  {
    if ( ranges.empty() ) {
      return false;
    }
    const uint32_t top = ranges.back().second - snd_una;
    at = static_cast<int32_t>( rexmit_nxt - snd_una ) < 0 ? 0 : rexmit_nxt - snd_una;
    gap_end = top;
    for ( size_t i = 0; i < ranges.size(); i++ ) {
      const uint32_t first = ranges[i].first - snd_una, second = ranges[i].second - snd_una;
      if ( at < first ) {
        gap_end = first;
        break;
      }
      if ( at < second ) {
        at = second;
      }
    }
    return at < top;
  }
};

}

#endif
