/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef STREAMCHILD_HPP
#define STREAMCHILD_HPP

#include <functional>
#include <string>
#include <vector>

#include <sys/types.h>

/*
 * The process at each end of a session's stream (see TYPE_STREAM): what the stream is a stream
 * between. It is described by words, kept together in one string with the character 0x1F between
 * them; the first word names it. Which names there are, and what each becomes, forwarding does
 * not know: that is the application's, and the program says (src/seam/applications.h). So
 * forwarding, which carries, knows no application (the layers: src/README.md).
 */

namespace Network {

std::vector<std::string> stream_words( const std::string& joined );

/*
 * What the process becomes: given a socket joined to the stream, it runs and returns its exit
 * status. It is called in the new process, with signals as the system starts them, every other
 * descriptor but standard input, output and error closed, and, when not local, standard error
 * going nowhere and the working directory home.
 */
typedef std::function<int( int sock )> StreamApplication;

/*
 * Starts the process and returns a socket joined to it, or -1. "local" says it is at the user's
 * end, where it may say things on standard error. With pid, the caller will wait for it;
 * without, nobody need.
 */
int stream_child( const StreamApplication& application, bool local, pid_t* pid );

}

#endif
