/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef GOSH_SEAM_APPLICATIONS_H
#define GOSH_SEAM_APPLICATIONS_H

#include <string>
#include <vector>

#include "src/forwarding/streamchild.h"

/*
 * The applications a session's stream can be between, by the name its words begin with. Glue:
 * forwarding carries a stream and knows no application; copy is one and knows no stream; this is
 * where the two are put together (the layers: src/README.md).
 *
 *   @copy WORDS...   one end of a file copy (src/copy/filecopy.h)
 *   @stdio           this program's own standard input and output
 *   @sh COMMAND      a command, run by the user's shell as ssh would run it
 *   @connect HOST PORT  a TCP connection to HOST:PORT from this end, joined to the stream both
 *                    ways, each way ending on its own (-W, REQ19/FWD-6); if it cannot be made,
 *                    it says why on standard error and the stream ends
 */
namespace Gosh {

/* Whether the words name an application, with words it takes. */
bool stream_words_valid( const std::vector<std::string>& words );

/* A port as @connect takes it: decimal, 1 to 65535. */
bool connect_port_valid( const std::string& port );

/* What the words name, to be started by Gosh::Forwarding::stream_child. The words must be valid. */
Gosh::Forwarding::StreamApplication stream_application( const std::vector<std::string>& words, bool local );

}

#endif
