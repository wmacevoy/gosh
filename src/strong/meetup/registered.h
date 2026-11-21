/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, what a REGISTERED says once it is opened: where the meetup saw the end (REQ18/PATH-5;
 * meetup/messages.h says the text). Logic, and a domain: unsigned numbers only, no pointer, no call
 * out but to the text.
 *
 * The text is 19 bytes, a where: a family, 4 or 6, then sixteen bytes of address and two of port.
 * Anything else is not read, and what was kept stays as it was. It keeps the family; the end
 * compares where's bytes, where they lie, with where it believes it is.
 *
 * It reads the text as a fixed<19, Registered> (subarray.h): that it has 19 bytes is its type,
 * found where it is made, so a text of another length never reaches it.
 *
 * Proved of it (the project strong-domain-driven-design), for every text of 19 bytes: read ends
 * and reads nothing outside the text, and the family kept is 0, 4 or 6.
 */

#ifndef GOSH_STRONG_MEETUP_REGISTERED_H
#define GOSH_STRONG_MEETUP_REGISTERED_H

#include <cstdint>

#include "src/strong/subarray.h"

namespace Strong {
namespace Meetup {

class Registered
{
private:
  uint32_t family; /* 0: nothing read; else 4 or 6, where's first byte */

public:
  Registered() : family( 0 ) {}

  /* True, and the family is kept, if this is a REGISTERED's text; else false, and what is kept is
     as it was. */
  bool read_checked( fixed<19, Registered> text )
  {
    uint32_t f = text.at( 0 );
    if ( f != 4 && f != 6 ) {
      return false;
    }
    family = f;
    return true;
  }

  /* 0 for nothing read; else 4 or 6. */
  uint32_t family_checked( void ) const { return family; }

  bool invariant() const { return family == 0 || family == 4 || family == 6; }
};

}
}

#endif
