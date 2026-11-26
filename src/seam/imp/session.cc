/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/seam/imp/session.h"

#include <cstdio>
#include <cstdlib>

#include "vendor/retain/retain.hpp"

/*
 * The session of the frame that retained one (vendor/retain/README.md): gosh's main makes a Session
 * and retains it before it calls the core's main, and the seam's hooks, which the core calls with
 * nothing, find it here. It is that thread's, and ends with main's frame: nothing is the whole
 * process's. (Until 2026-11-19 it was one static value; design v2, 4.2.)
 *
 * Expects that a Session is retained. If none is, that is a fault in gosh's own code, and the
 * process ends saying so.
 */
Gosh::Seam::Imp::Session& Gosh::Seam::Imp::session( void )
{
  if ( !retained<Session>() ) {
    fprintf( stderr, "gosh: no session is retained (gosh's main retains one before the core runs)\n" );
    abort();
  }
  return *recall<Session>();
}
