/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * The seam's one value: the session of gosh's in this process (design v2, 4.2). A program holds
 * exactly one mosh session, so there is exactly one of these; gosh's main fills it in before
 * the core's main runs, and the hooks (hooks.h) work on it.
 */

#ifndef GOSH_SEAM_SESSION_H
#define GOSH_SEAM_SESSION_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <sys/socket.h>

#include "src/forwarding/forwarding.h"
#include "src/seam/options.h"
#include "src/seam/relaying.h"
#include "src/seam/streaming.h"

namespace Gosh {

struct Session
{
  ForwardOptions options; /* what the program was asked to forward */
  bool client;            /* which end this is */
  std::unique_ptr<Network::Forwarding> forwarding;
  std::vector<int> watched; /* forwarding's descriptors, as last given to the program's loop */
  bool announced;           /* whether a headless client has said that it is connected */

  StreamState stream; /* a stream: the process at this end (streaming.h) */
  RelayState relay;   /* the relay, the client's (relaying.h) */

  /* the session's own state, as the client's loop cannot be handed it each pass (set by session_started) */
  std::function<bool()> ending;         /* the session is being ended */
  std::function<void()> end_it;         /* begin to end it */
  std::function<uint64_t()> last_heard; /* when the server was last heard */
  std::function<void( const Network::Addr&, socklen_t )> send_elsewhere; /* send to here from now on */
  uint64_t started_at;
  bool loop_returned; /* the client's loop came to its end, and was not thrown out of */

  Session()
    : options(), client( false ), forwarding(), watched(), announced( false ), stream(), relay(), ending(), end_it(),
      last_heard(), send_elsewhere(), started_at( 0 ), loop_returned( false )
  {}
};

Session& session( void );

/* Whether this program may yet start a process: a stream's, or the relay. (It must not give up the right to.) */
inline bool may_start_processes( void )
{
  return !session().options.stream.empty() || session().relay.mode != RELAY_NEVER;
}

/* Whether this session is for a stream. */
inline bool has_stream( void )
{
  return !session().options.stream.empty();
}

/* Whether this client has no terminal (REQ1/FWD-5). */
inline bool headless( void )
{
  return session().options.headless;
}

}

#endif
