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

namespace Gosh::Seam::Imp {

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

/* What the stream is to start, from the words the program was given (none if it is not for a stream). */
void take_stream_words( void );

/* The server: its end of the stream is to be started when the client asks for it. */
void serve_stream( void );

/* The client, once the session is up: starts the process at its end, and asks the server for the other. */
void start_stream( void );

/* The client: the process at its end will never run (it could not be started, or the server cannot do streams). */
void stream_never_ran( void );

}

#endif
