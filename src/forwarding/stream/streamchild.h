/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef STREAMCHILD_HPP
#define STREAMCHILD_HPP

#include <string>
#include <vector>

#include <sys/types.h>

/*
 * The process at each end of a session's stream (see TYPE_STREAM): what
 * the stream is a stream between.  It is described by words, kept
 * together in one string with the character 0x1F between them:
 *
 *   @copy WORDS...   one end of a file copy (filecopy.h)
 *   @stdio           this program's own standard input and output
 *   @sh COMMAND      a command, run by the user's shell as ssh would run it
 */

namespace Network {

std::vector<std::string> stream_words( const std::string& joined );

/* Whether the words describe something that can be started. */
bool stream_words_valid( const std::vector<std::string>& words );

/*
 * Starts the process and returns a socket joined to it, or -1.  "local"
 * says it is at the user's end, where it may say things on standard
 * error.  With pid, the caller will wait for it; without, nobody need.
 */
int stream_child( const std::vector<std::string>& words, bool local, pid_t* pid );

}

#endif
