/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * The seam, asked: what the core asks of gosh where the seam's patches call it, beside the turns
 * of its loop (hooks.h) and its mains (core-main.h). Of the seam's ABI, so that the core calls
 * nothing of gosh's inside.
 */

#ifndef GOSH_SEAM_ASKED_H
#define GOSH_SEAM_ASKED_H

#include "src/seam/imp/session.h"

namespace Gosh::Seam {

/* Whether this program may yet start a process: a stream's, or the relay. (It must not give up the right to.) */
inline bool may_start_processes( void )
{
  return !Imp::session().options.stream.empty() || Imp::session().relay.mode != Gosh::Relay::RELAY_NEVER;
}

/* Whether this session is for a stream. */
inline bool has_stream( void )
{
  return !Imp::session().options.stream.empty();
}

/* Whether this client has no terminal (REQ1/FWD-5). */
inline bool headless( void )
{
  return Imp::session().options.headless;
}

/*
 * The client, for a stream: the status to exit with. We were only the means; what matters is
 * how the process the stream was for got on: its exit status; or 1 if it never ran, or if the
 * client's loop was thrown out of before it could be asked.
 * success: whether the session itself ended cleanly.
 */
int stream_exit( bool success );

}

#endif
