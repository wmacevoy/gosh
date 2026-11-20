/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/*
 * What the cipher gave back, opened: its bytes, one after another (REQ6/SHAPE-5). A type that says
 * what its readers may expect (subarray.h): that many bytes are there to be read, and they stay as
 * they are while the reader runs. Whoever makes one keeps that: nothing tests it. It says nothing of
 * who sealed them.
 */
#ifndef GOSH_STRONG_OPENED_H
#define GOSH_STRONG_OPENED_H
#include "src/strong/subarray.h"
namespace Strong {
typedef subarray<const unsigned char*> Opened;
}
#endif
