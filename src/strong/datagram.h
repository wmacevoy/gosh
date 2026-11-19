/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * A datagram as it was received: its bytes, one after another (REQ6/SHAPE-5). A type that says
 * what its readers may expect (subarray.h): that many bytes are there to be read, and they stay
 * as they are while the reader runs. Whoever makes one keeps that: nothing tests it.
 */

#ifndef GOSH_STRONG_DATAGRAM_H
#define GOSH_STRONG_DATAGRAM_H

#include "src/strong/subarray.h"

namespace Strong {

typedef subarray<const unsigned char*> Datagram;

}

#endif
