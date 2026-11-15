/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/seam/session.h"

/*
 * The one process-wide value the seam has (design v2, 4.2). It rests on "one mosh session per
 * process", which is true of both programs. It is made on first use, so its order among other
 * statics does not matter.
 */
Gosh::Session& Gosh::session( void )
{
  static Session the_session;
  return the_session;
}
