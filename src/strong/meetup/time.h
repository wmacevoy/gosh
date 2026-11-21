/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, what a TIME says once it is opened: the counter of the REGISTER it answers, and the
 * meetup's time (REQ18/PATH-5; meetup/messages.h says the text). Logic, and a domain: unsigned
 * numbers only, no pointer, no call out but to the text.
 *
 * The text is 16 bytes: the counter (8), then the time in milliseconds (8), each most significant
 * first. A counter is under 2^62 (counter.h), so a text that names a larger one answers no REGISTER
 * there can be, and is not read. Anything else is not read, and what was kept stays as it was.
 *
 * It reads the text as a fixed<16, Time> (subarray.h): that it has 16 bytes is its type, found
 * where it is made, so a text of another length never reaches it.
 *
 * Proved of it (MeetupTime.proof.cpp, beside it; tools/proofs.sh), for every text of 16 bytes: read ends
 * and reads nothing outside the text, and the counter kept is a counter there can be. Not proved:
 * that the numbers kept are the text's, most significant first.
 */

#ifndef GOSH_STRONG_MEETUP_TIME_H
#define GOSH_STRONG_MEETUP_TIME_H

#include <cstdint>

#include "src/strong/meetup/counter.h"
#include "src/strong/subarray.h"

namespace Strong {
namespace Meetup {

class Time
{
private:
  uint64_t answering; /* the counter of the REGISTER answered; 0 if nothing read */
  uint64_t time;      /* ms by the meetup's clock; 0 if nothing read */

public:
  Time() : answering( 0 ), time( 0 ) {}

  /* True, and the counter and time are kept, if this is a TIME's text; else false, and what is
     kept is as it was. */
  bool read_checked( fixed<16, Time> text )
  {
    uint64_t a = text.at( 0 );
    a = ( a << 8 ) | text.at( 1 );
    a = ( a << 8 ) | text.at( 2 );
    a = ( a << 8 ) | text.at( 3 );
    a = ( a << 8 ) | text.at( 4 );
    a = ( a << 8 ) | text.at( 5 );
    a = ( a << 8 ) | text.at( 6 );
    a = ( a << 8 ) | text.at( 7 );
    if ( a > COUNTER_MOST ) {
      return false;
    }
    uint64_t t = text.at( 8 );
    t = ( t << 8 ) | text.at( 9 );
    t = ( t << 8 ) | text.at( 10 );
    t = ( t << 8 ) | text.at( 11 );
    t = ( t << 8 ) | text.at( 12 );
    t = ( t << 8 ) | text.at( 13 );
    t = ( t << 8 ) | text.at( 14 );
    t = ( t << 8 ) | text.at( 15 );
    answering = a;
    time = t;
    return true;
  }

  /* Under 2^62. */
  uint64_t answering_checked( void ) const { return answering; }

  uint64_t time_checked( void ) const { return time; }

  bool invariant() const { return answering <= COUNTER_MOST; }
};

}
}

#endif
