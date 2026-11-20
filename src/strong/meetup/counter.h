/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, a counter: the number an end seals a message with, 62 bits (REQ18/PATH-5; messages.h:
 * the two bits above them say who sealed it and for whom). A type that says what its users may
 * expect: a Counter is from 0 to COUNTER_MOST (bounded.h). Nothing is tested where
 * one is made or used: whoever makes one owes that the number is within the bounds, and only a
 * proved class may. Everything else makes a CounterChecked, which tests.
 *
 * It is here so that what CounterWindow (counterwindow.h) expects of its caller is said by the type
 * it takes, and is proved, not trusted (the project strong-domain-driven-design): given any 64-bit
 * number, CounterWindow at 2^64 - 1 forgot counters it had taken (found there, 2026-11-18; never
 * reached, since every counter came through that mask already).
 */

#ifndef GOSH_STRONG_MEETUP_COUNTER_H
#define GOSH_STRONG_MEETUP_COUNTER_H

#include <cstdint>

#include "src/strong/bounded.h"

namespace Strong {
namespace Meetup {

static const uint64_t COUNTER_MOST = 0x3fffffffffffffff; /* 2^62 - 1 */

typedef bounded<0, COUNTER_MOST, uint64_t> Counter;

/* Any number, tested where it is made, and the process ended if it is not a counter: what code
   that is not proved hands to CounterWindow::take_checked. */
typedef bounded_checked<0, COUNTER_MOST, uint64_t> CounterChecked;

}
}

#endif
