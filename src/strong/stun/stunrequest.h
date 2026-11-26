/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * Whether bytes are a STUN Binding Request (REQ15/PATH-4, REQ18/PATH-5): the first thing a meetup
 * asks of a datagram from anyone. Logic, and a domain: it reads only below the datagram's size (datagram.h).
 */

#ifndef GOSH_STRONG_PATHFINDING_STUNREQUEST_H
#define GOSH_STRONG_PATHFINDING_STUNREQUEST_H

#include "src/strong/datagram.h"

namespace Strong {
namespace Stun {

/* a Binding Request, with the cookie: and nothing longer than one need be */
inline bool is_stun_request_checked( Datagram request )
{
  return !( request.size() < 20 || request.size() > 200 || request.at( 0 ) != 0x00 || request.at( 1 ) != 0x01
            || request.at( 4 ) != 0x21 || request.at( 5 ) != 0x12 || request.at( 6 ) != 0xA4 || request.at( 7 ) != 0x42 );
}

}
}

#endif
