/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * forwarding, TCP: the options of a segment, read (REQ1/FWD-1). Logic.
 *
 * Options are a list of kind, length, value; kind 1 is padding and kind 0 ends the list. A list
 * that is cut short or has a length that cannot be is read as far as it is sound and no further:
 * what the other end did not manage to say, it did not say.
 */

#ifndef GOSH_TCP_OPTIONS_H
#define GOSH_TCP_OPTIONS_H

#include <algorithm>
#include <cstdint>
#include <string>

namespace Gosh::Forwarding::Imp {

struct SegmentOptions
{
  /*
   * Calls see( kind, at, length ) for each option in the list, in order, until it returns false:
   * `at` is where the option's value begins in `options`, and `length` how many bytes it has
   * (its whole length less the kind and the length bytes). Padding (kind 1) is passed over; the
   * end of the list (kind 0), an option cut short, or a length that cannot be, ends the walk.
   * Every reader of options goes through this, so that each reads a list the same way.
   */
  template<class See>
  static void walk( const std::string& options, See see )
  {
    for ( size_t i = 0; i < options.size(); ) {
      const uint8_t kind = options[i];
      if ( kind == 0 ) {
        break;
      }
      if ( kind == 1 ) {
        i++;
        continue;
      }
      if ( i + 1 >= options.size() ) {
        break;
      }
      const uint8_t len = options[i + 1];
      if ( len < 2 || i + len > options.size() ) {
        break;
      }
      if ( !see( kind, i + 2, static_cast<size_t>( len - 2 ) ) ) {
        break;
      }
      i += len;
    }
  }

  /* The largest segment the other end asks for (kind 2), or `otherwise` if it did not say. */
  static uint32_t mss( const std::string& options, uint32_t otherwise )
  {
    uint32_t found = otherwise;
    walk( options, [&]( uint8_t kind, size_t at, size_t length ) {
      if ( kind == 2 && length == 2 ) {
        found = ( static_cast<uint8_t>( options[at] ) << 8 ) | static_cast<uint8_t>( options[at + 1] );
        return false;
      }
      return true;
    } );
    return found;
  }

  /* The window scale option: the shift asked for, or -1 if it was not. */
  static int scale( const std::string& options )
  {
    int found = -1;
    walk( options, [&]( uint8_t kind, size_t at, size_t length ) {
      if ( kind == 3 && length == 1 ) {
        found = std::min<int>( static_cast<uint8_t>( options[at] ), 14 );
        return false;
      }
      return true;
    } );
    return found;
  }

  /* Whether the options of a SYN say "selective acknowledgment permitted" (kind 4). */
  static bool sack_permitted( const std::string& options )
  {
    bool found = false;
    walk( options, [&]( uint8_t kind, size_t, size_t length ) {
      if ( kind == 4 && length == 0 ) {
        found = true;
        return false;
      }
      return true;
    } );
    return found;
  }

  static void put32( std::string& to, uint32_t v )
  {
    const char b[4] = { static_cast<char>( v >> 24 ), static_cast<char>( v >> 16 ), static_cast<char>( v >> 8 ),
                        static_cast<char>( v ) };
    to.append( b, 4 );
  }

  static uint32_t get32( const std::string& from, size_t at )
  {
    return ( static_cast<uint32_t>( static_cast<uint8_t>( from[at] ) ) << 24 )
           | ( static_cast<uint32_t>( static_cast<uint8_t>( from[at + 1] ) ) << 16 )
           | ( static_cast<uint32_t>( static_cast<uint8_t>( from[at + 2] ) ) << 8 )
           | static_cast<uint32_t>( static_cast<uint8_t>( from[at + 3] ) );
  }
};

}

#endif
