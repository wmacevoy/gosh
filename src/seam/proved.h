/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * Tooling for the parts' unit tests: a fact that is proved for every input need not also be tried
 * on one, while working. The lite check (tools/check.sh --lite, which sets GOSH_CHECK=lite) leaves
 * such a fact out and says so; the whole check tries it all the same, since what is proved rests
 * on the prover's reading of the C++, which is itself only tested.
 *
 *     if ( !Gosh::proved_so_not_tried( "what the fact says", "Strong::Class, proofs/Class.proof.cpp" ) ) {
 *       FACT_CHECK( ... );
 *     }
 *
 * Mark a fact so only where the proof says the same thing as the fact, of the same code. A fact
 * that also goes through code that is not proved (an edge, the cipher) is left as it is, unless
 * another fact beside it goes the same way.
 */

#ifndef GOSH_SEAM_PROVED_H
#define GOSH_SEAM_PROVED_H

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Gosh {

/* True, and a line saying so, in the lite check: the fact named is proved (by `proof`, in the
   project strong-domain-driven-design) and is not tried now. False in every other run. */
inline bool proved_so_not_tried( const char* fact, const char* proof )
{
  const char* mode = getenv( "GOSH_CHECK" );
  if ( mode == nullptr || strcmp( mode, "lite" ) != 0 ) {
    return false;
  }
  printf( "SKIPPED (proved): %s: %s\n", fact, proof );
  return true;
}

}

#endif
