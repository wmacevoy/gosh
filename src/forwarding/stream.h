/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING). gosh carries mosh's code
    and is not the mosh project's work. The rule here is tend_stream's, in
    wmacevoy/mosh at mosh-1.4.0-pf.7 (src/frontend/stmclient.cc).
*/

#ifndef GOSH_FORWARDING_STREAM_HPP
#define GOSH_FORWARDING_STREAM_HPP

#include <cstddef>
#include <cstdint>

/*
 * forwarding: stream (design v2, 2.2). When the client of a session that exists for a stream
 * has finished (REQ1/CP-7).
 *
 * Logic only: numbers in, an answer out.
 */
namespace Gosh {

/* How long, after its own process has ended, the client waits for the last of the stream to go (ms). */
const uint64_t STREAM_GRACE = 5000;

/*
 * Whether the client should now begin to end the session: its process at this end of the stream
 * has ended, and either everything that process sent has gone (no stream connection is left) or
 * STREAM_GRACE has passed since it ended. Never while the session is already ending.
 *
 *   process_ended   the process at this end has ended (or could never be started)
 *   ended_at        when it ended
 *   connections     how many stream connections there still are
 *   now             the time, on the same clock as ended_at
 *   ending          the session is already being ended
 */
inline bool stream_over( bool process_ended, uint64_t ended_at, size_t connections, uint64_t now, bool ending )
{
  return process_ended && !ending && ( connections == 0 || now - ended_at > STREAM_GRACE );
}

}

#endif
