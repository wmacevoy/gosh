/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, what a PASS says once it is opened: a datagram of the session's, for the meetup to send
 * on to the other end (REQ18/PATH-5; meetup/imp/messages.h says the text). Logic, and a domain:
 * unsigned numbers only, no pointer, no call out but to the text.
 *
 * The text is the datagram, 1 to 1,500 bytes. Anything else is not read, and what was kept stays
 * as it was. It keeps the length; the meetup sends the bytes on from where they lie
 * (meetup/imp/sealed.h: Unsealed<T>::read_onto), and looks at none of them: they are sealed already,
 * under the session's key, which the meetup does not have.
 *
 * It reads the text as an Opened<Pass> (opened.h): its length is whatever the cipher gave back.
 *
 * Proved of it (MeetupPass.proof.cpp, beside it; tools/proofs.sh), for every text: read ends and reads
 * nothing outside the text, the length kept is at most 1,500, and one kept is the text's.
 *
 * The number is meetup/imp/messages.h's, written out: it asks the compiler that they are the same.
 */

#ifndef GOSH_STRONG_MEETUP_PASS_H
#define GOSH_STRONG_MEETUP_PASS_H

#include <cstdint>

#include "src/strong/opened.h"

namespace Strong {
namespace Meetup {

/* The most a datagram passed on may be. */
static const uint64_t DATAGRAM_MOST = 1500;

class Pass
{
private:
  uint64_t length; /* 0: nothing read; else the datagram's, 1 to DATAGRAM_MOST */

public:
  Pass() : length( 0 ) {}

  /* True, and its length is kept, if this is a PASS's text; else false, and what is kept is as
     it was. */
  bool read_checked( Opened<Pass> text )
  {
    uint64_t n = text.size();
    if ( n < 1 || n > DATAGRAM_MOST ) {
      return false;
    }
    length = n;
    return true;
  }

  /* 0 for nothing read; else 1 to DATAGRAM_MOST. */
  uint64_t length_checked( void ) const { return length; }

  bool invariant() const { return length <= DATAGRAM_MOST; }
};

}
}

#endif
