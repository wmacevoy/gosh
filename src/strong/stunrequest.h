/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * Whether bytes are a STUN Binding Request (REQ15/PATH-4, REQ18/PATH-5): the first thing a meetup
 * asks of a datagram from anyone. Logic, and a domain: it reads only below the length it is given.
 */

#ifndef GOSH_STRONG_STUNREQUEST_H
#define GOSH_STRONG_STUNREQUEST_H

#include <cstddef>

namespace Strong {

/* a Binding Request, with the cookie: and nothing longer than one need be */
inline bool is_stun_request( const char* request, size_t n )
{
  const unsigned char* p = reinterpret_cast<const unsigned char*>( request );
  return !( n < 20 || n > 200 || p[0] != 0x00 || p[1] != 0x01 || p[4] != 0x21 || p[5] != 0x12 || p[6] != 0xA4
            || p[7] != 0x42 );
}

}

#endif
