/*
    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

#include "src/seam/hooks.h"

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
