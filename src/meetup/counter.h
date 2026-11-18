/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, a counter: the number an end seals a message with, 62 bits (REQ18/PATH-5; messages.h:
 * the two bits above them say who sealed it and for whom). Logic, and a type that says what its
 * users may expect: a MeetupCounter is at most MOST, however it was made, because its constructor
 * keeps only those 62 bits. Nothing is checked where one is used.
 *
 * It is here so that what CounterWindow (registrations.h) expects of its caller is said by the type
 * it takes, and is proved, not trusted (the project strong-domain-driven-design): given any 64-bit
 * number, CounterWindow at 2^64 - 1 forgot counters it had taken (found there, 2026-11-18; never
 * reached, since every counter came through this mask already).
 */

#ifndef GOSH_MEETUP_COUNTER_H
#define GOSH_MEETUP_COUNTER_H

#include <cstdint>

namespace Gosh {

class MeetupCounter
{
public:
  static const uint64_t MOST = 0x3fffffffffffffff; /* 2^62 - 1 */

private:
  uint64_t value;

public:
  /* The counter in a nonce: its low 62 bits. */
  explicit MeetupCounter( uint64_t nonce ) : value( nonce & MOST ) {}

  uint64_t number( void ) const { return value; }

  bool invariant() const { return value <= MOST; }
};

}

#endif
