/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * The seam, a stream (REQ1/CP-7): what the client keeps of the process at its end of a
 * stream, and the look it takes at it each turn. The server's process is forwarding's to start.
 */

#ifndef GOSH_SEAM_STREAMING_H
#define GOSH_SEAM_STREAMING_H

#include <cstdint>
#include <string>
#include <vector>

#include <sys/types.h>

namespace Gosh {

struct StreamState
{
  std::vector<std::string> words; /* what to start; empty if the session is not for a stream */
  pid_t pid;                      /* 0 before it starts, -1 once it has ended or could not start */
  int status;                     /* how it ended, as an exit status */
  uint64_t ended;                 /* when */

  StreamState() : words(), pid( 0 ), status( 1 ), ended( 0 ) {}
};

/* The client, for a stream: sees whether its process has ended, and ends the session when the stream is over. */
void tend_stream( void );

}

#endif
