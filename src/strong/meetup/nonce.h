/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * What a meetup message is sealed with (REQ18/PATH-5): a nonce is who sealed it, for which end,
 * and a counter. Logic, and a domain: private state, numbers only, nothing of the system. Its
 * users are meetup/messages.h's meetup_seal and meetup_open, which are not proved.
 *
 * A ticket's key has four senders: each of the two ends, and the meetup to each. The cipher is
 * safe only if no two messages under one key are sealed with the same nonce. So the top two bits
 * of a nonce say the sender (bit 63: the meetup sealed it; bit 62: which end it is from, or
 * for), and the 62 bits below are that sender's counter.
 *
 * What is proved of it (the project strong-domain-driven-design, proofs/Nonce.proof.cpp):
 * every 64-bit nonce reads as one sender and one counter, and written again is the same nonce;
 * and a sender and a counter below 2^62, written as a nonce and read again, are the same. So two
 * senders, or two counters of one sender, never make one nonce.
 *
 * What is NOT here: that a sender's counter is below 2^62 and is never used twice. set_checked
 * keeps the low 62 bits of whatever it is handed, as meetup_seal always did.
 */

#ifndef GOSH_STRONG_MEETUP_NONCE_H
#define GOSH_STRONG_MEETUP_NONCE_H

#include <cstdint>

#include "src/strong/meetup/counter.h"

namespace Strong {
namespace Meetup {

class Nonce
{
private:
  uint32_t end;       /* 0 or 1: the end it is from, or for */
  uint32_t by_meetup; /* 1: the meetup sealed it; 0: that end did */
  uint64_t counter;   /* at most 2^62 - 1 */

public:
  Nonce() : end( 0 ), by_meetup( 0 ), counter( 0 ) {}

  /* This sender, and this counter's low 62 bits. An end, or a sealer, that is not 0 is 1. */
  void set_checked( uint32_t an_end, uint32_t sealed_by_meetup, uint64_t a_counter )
  {
    end = 0;
    if ( an_end != 0 ) {
      end = 1;
    }
    by_meetup = 0;
    if ( sealed_by_meetup != 0 ) {
      by_meetup = 1;
    }
    counter = a_counter & COUNTER_MOST;
  }

  /* The sender and the counter this nonce says. Every nonce says one of each. */
  void read_checked( uint64_t nonce )
  {
    end = static_cast<uint32_t>( ( nonce >> 62 ) & 1 );
    by_meetup = static_cast<uint32_t>( nonce >> 63 );
    counter = nonce & COUNTER_MOST;
  }

  /* The nonce to seal with. */
  uint64_t value_checked( void ) const
  {
    return ( static_cast<uint64_t>( by_meetup ) << 63 ) | ( static_cast<uint64_t>( end ) << 62 ) | counter;
  }

  uint32_t end_checked( void ) const { return end; }

  uint32_t by_meetup_checked( void ) const { return by_meetup; }

  uint64_t counter_checked( void ) const { return counter; }

  /* A sender there is, and a counter of 62 bits: so value_checked() loses nothing. */
  bool invariant() const { return end <= 1 && by_meetup <= 1 && counter <= COUNTER_MOST; }
};

}
}

#endif
