/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, proof: the twelve bytes that show their sender holds a meetup's key (REQ15/PATH-5).
 * Logic: bytes and a key in, bytes or yes/no out (the making of one draws four bytes at random).
 * The fork's text (mosh-1.4.0-pf.7, src/network/punch.{h,cc}).
 *
 * What a proof does NOT show (docs/FINDINGS.md, 2026-11-13; proof-test.cc says it with the
 * real cipher): who sent it, when, or for which pair of ends. It is four bytes and a tag of
 * those four bytes alone, and it travels in the clear at the front of every datagram a meetup
 * is asked to pass on. Whoever sees one datagram holds a proof that is good for ever, from
 * anywhere, for anything the meetup does.
 */

#ifndef GOSH_MEETUP_PROOF_H
#define GOSH_MEETUP_PROOF_H

#include <cstddef>
#include <string>

namespace Network {

/* The mark that begins a datagram for a meetup to pass on, or that one has passed on. */
extern const char MEETUP_MARK[8];
static const size_t MEETUP_HEADER = 8 + 12 + 16; /* the mark; proof of the key; which pair of ends */

/*
 * Twelve bytes that show their sender holds a meetup's key (meetup_serve,
 * below): four at random, and eight that only the key gives for those
 * four.  They go where a STUN request has its transaction id, and to a
 * server that knows nothing of this they are twelve bytes like any
 * others.  key: as Crypto::Base64Key prints it.
 */
std::string meetup_proof( const std::string& key );
bool meetup_proof_ok( const char* txid, const std::string& key );

}

#endif
