/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * The seam, the relay (REQ1/PATH-1): what the client keeps of its local relay, and the two
 * things it does about it. When to do them is src/relay/start.h's to say.
 */

#ifndef GOSH_SEAM_RELAYING_H
#define GOSH_SEAM_RELAYING_H

#include <cstdint>
#include <string>
#include <vector>

#include <sys/types.h>

#include "src/relay/start.h"

namespace Gosh::Seam::Imp {

struct RelayState
{
  Gosh::Relay::RelayMode mode;
  std::vector<std::string> command;   /* starts the remote relay, through ssh as a rule */
  std::string server_ip, server_port; /* where the server is, as the client was told */
  bool engaged;                       /* it was started, or tried, once */
  int keep_alive;                     /* the local relay runs until this closes, which it does when we exit */
  pid_t pid;                          /* the local relay's process, while there is one: it is our child */
  bool gone;                          /* it died, and another has not been started yet */
  uint64_t started_at;

  RelayState()
    : mode( Gosh::Relay::RELAY_NEVER ), command(), server_ip(), server_port(), engaged( false ), keep_alive( -1 ), pid( 0 ),
      gone( false ), started_at( 0 )
  {}
};

/* The client: starts the local relay, and talks to the server through it from now on. */
void engage_relay( void );

/*
 * The client, each turn: if the server has gone quiet and there is a relay to start, starts it;
 * and if the relay it started has died, says so and starts another (start.h has both rules).
 */
void consider_relay( void );

}

#endif
