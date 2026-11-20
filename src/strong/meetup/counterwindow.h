/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * Which counters an end of the meetup has used (REQ18/PATH-5): the guard against a message sent
 * twice. Logic, and a domain: private state, no pointer, nothing of the system. Its user is
 * meetup/registrations.h, which is not proved, and comes in by take_checked.
 */

#ifndef GOSH_STRONG_MEETUP_COUNTERWINDOW_H
#define GOSH_STRONG_MEETUP_COUNTERWINDOW_H

#include <cstdint>
#include <cstring>

#include "src/strong/meetup/counter.h"

namespace Strong {

/* Which counters an end has used, of the last WINDOW: the highest, and a mark for each below it.
 * The first counter taken is the lowest ever taken: nothing sealed before it is. */
class CounterWindow
{
public:
  static const uint64_t WINDOW = 1024;

private:
  bool any;
  uint64_t top;
  uint64_t seen[WINDOW / 64];

  bool test( uint64_t c ) const { return ( seen[( c % WINDOW ) / 64] >> ( c % 64 ) ) & 1; }
  void set( uint64_t c ) { seen[( c % WINDOW ) / 64] |= uint64_t( 1 ) << ( c % 64 ); }
  void clear( uint64_t c ) { seen[( c % WINDOW ) / 64] &= ~( uint64_t( 1 ) << ( c % 64 ) ); }

public:
  CounterWindow() : any( false ), top( 0 ), seen() {}

  /* True, and the counter marked used, if it has not been used and is not too far behind. What
     it is given is at most 2^62 - 1: the type says so (meetupcounter.h), so `top + 1` and `i` below
     never pass the end of 64 bits. */
  bool take( MeetupCounter counter )
  {
    const uint64_t c = counter.number();
    if ( !any ) {
      any = true;
      top = c;
      memset( seen, 0xff, sizeof( seen ) ); /* everything behind it, as if used */
      return true;
    }
    if ( c > top ) {
      for ( uint64_t i = top + 1; i <= c && i - top <= WINDOW; i++ ) {
        clear( i );
      }
      top = c;
      set( c );
      return true;
    }
    if ( top - c >= WINDOW || test( c ) ) {
      return false;
    }
    set( c );
    return true;
  }

  /* The same, for code that is not proved: a counter that was tested when it was made
     (meetupcounter.h; the process ended there if it was not one). */
  bool take_checked( MeetupCounterChecked given )
  {
    MeetupCounter counter( given.number() );
    return take( counter );
  }

  /* The highest counter taken is a counter: at most 2^62 - 1. True only because of what take() is
     given; so `top + 1` above is never past the end of 64 bits. */
  bool invariant() const { return !any || top <= MEETUP_COUNTER_MOST; }
};

}

#endif
