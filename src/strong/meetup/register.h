/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, what a REGISTER says once it is opened: where the end believes it is, and the time by the
 * meetup's clock as the end reckons it (REQ18/PATH-5; meetup/messages.h says the text). Logic, and a
 * domain: unsigned numbers only, no pointer, no call out but to the text.
 *
 * The text is 27 bytes: where (19: a family, 4 or 6, then sixteen bytes of address and two of port),
 * then the time in milliseconds, eight bytes, most significant first. Anything else is not read, and
 * what was kept stays as it was. It keeps the family and the time, as numbers; where's bytes are its
 * first 19, which the caller copies out.
 *
 * Proved of it (the project strong-domain-driven-design), for every sequence of bytes of any length:
 * read ends and reads nothing outside the text; the family kept is 0, 4 or 6; and a family other
 * than 0 is kept only from a text of 27 bytes. Not proved: that the time kept is the last eight
 * bytes, most significant first.
 *
 * The numbers are meetup/messages.h's, written out: it asks the compiler that they are the same.
 */

#ifndef GOSH_STRONG_MEETUP_REGISTER_H
#define GOSH_STRONG_MEETUP_REGISTER_H

#include <cstdint>

#include "src/strong/opened.h"

namespace Strong {
namespace Meetup {

class Register
{
private:
  uint32_t family; /* 0: nothing read; else 4 or 6, where's first byte */
  uint64_t time;   /* ms by the meetup's clock, as the end reckons it; 0 if nothing read */

public:
  Register() : family( 0 ), time( 0 ) {}

  /* True, and the family and time are kept, if this is a REGISTER's text; else false, and what is
     kept is as it was. */
  bool read_checked( Opened<Register> text )
  {
    if ( text.size() != 27 ) {
      return false;
    }
    uint32_t f = text.at( 0 );
    if ( f != 4 && f != 6 ) {
      return false;
    }
    uint64_t t = text.at( 19 );
    t = ( t << 8 ) | text.at( 20 );
    t = ( t << 8 ) | text.at( 21 );
    t = ( t << 8 ) | text.at( 22 );
    t = ( t << 8 ) | text.at( 23 );
    t = ( t << 8 ) | text.at( 24 );
    t = ( t << 8 ) | text.at( 25 );
    t = ( t << 8 ) | text.at( 26 );
    family = f;
    time = t;
    return true;
  }

  /* 0 for nothing read; else 4 or 6. */
  uint32_t family_checked( void ) const { return family; }

  uint64_t time_checked( void ) const { return time; }

  /* Nothing read, or a family there is. */
  bool invariant() const { return ( family == 0 && time == 0 ) || family == 4 || family == 6; }
};

}
}

#endif
