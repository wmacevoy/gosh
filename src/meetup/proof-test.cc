/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ15/PATH-5: the proof of a meetup's key (proof.h), with the real cipher: what it shows, and
 * what it does not. The second half is the finding of 2026-11-13, said as a test so that it
 * cannot be lost: each check there passes today, and each is a thing a reader would wish false.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/crypto/crypto.h"
#include "src/meetup/proof.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  using Network::meetup_proof;
  using Network::meetup_proof_ok;
  const std::string key = Crypto::Base64Key().printable_key();
  const std::string other_key = Crypto::Base64Key().printable_key();

  const std::string proof = meetup_proof( key );
  check( "a proof is twelve bytes", proof.size() == 12 );
  check( "made with the key, it is good for the key", meetup_proof_ok( proof.data(), key ) );
  check( "and not for another key", !meetup_proof_ok( proof.data(), other_key ) );
  bool any_good = false;
  for ( size_t i = 0; i < 12; i++ ) {
    std::string changed = proof;
    changed[i] ^= 0x01;
    any_good = any_good || meetup_proof_ok( changed.data(), key );
  }
  check( "with any one of its twelve bytes changed it is not good", !any_good );
  check( "twelve bytes of nothing are not good", !meetup_proof_ok( std::string( 12, '\0' ).data(), key ) );
  check( "two proofs made with one key differ (four bytes of each are drawn at random)", meetup_proof( key ) != proof );
  bool threw = false;
  try {
    meetup_proof( "not a key" );
  } catch ( const std::exception& e ) {
    threw = true;
  }
  check( "what is not a key makes no proof: it says so", threw );

  printf( "what a proof does not show (docs/FINDINGS.md, 2026-11-13):\n" );
  const std::string seen = proof; /* an onlooker's copy: they have the twelve bytes and not the key */
  check( "  a copy of a proof, in the hands of someone without the key, is good", meetup_proof_ok( seen.data(), key ) );
  check( "  and good again, and again: it is not used up", meetup_proof_ok( seen.data(), key ) && meetup_proof_ok( seen.data(), key ) );
  /* There is nothing else to check it against: the test of a proof takes the twelve bytes and the key, and no time, sender or pair. */
  check( "  nothing in it is a time, a sender or a pair of ends: the same twelve bytes are all there is to it", seen == proof );

  if ( failures ) {
    printf( "FAIL REQ15/PATH-5: %d checks of the proof of a meetup's key failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ15/PATH-5: the proof of a meetup's key, and what it does not show\n" );
  return EXIT_SUCCESS;
}
