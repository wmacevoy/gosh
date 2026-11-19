/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * A datagram as it was received: some first bytes of an array of DATAGRAM_MOST, which is more
 * than any UDP datagram holds (REQ6/SHAPE-5). A type that says what its readers may expect
 * (subarray.h): that many bytes are there to be read, and no more is asked of whoever hands one over.
 *
 * Code that is not proved makes a DatagramChecked, from the array it received into and the number
 * of bytes that came; a number larger than the array ends the process.
 */

#ifndef GOSH_STRONG_DATAGRAM_H
#define GOSH_STRONG_DATAGRAM_H

#include <cstdint>

#include "src/strong/subarray.h"

namespace Strong {

static const uint64_t DATAGRAM_MOST = 65536;

typedef subarray<unsigned char, DATAGRAM_MOST> Datagram;

typedef subarray_checked<unsigned char, DATAGRAM_MOST> DatagramChecked;

}

#endif
