/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef GOSH_SEAM_APPLICATIONS_H
#define GOSH_SEAM_APPLICATIONS_H

#include <string>
#include <vector>

#include "src/forwarding/stream/streamchild.h"

/*
 * The applications a session's stream can be between, by the name its words begin with. Glue:
 * forwarding carries a stream and knows no application; copy is one and knows no stream; this is
 * where the two are put together (the layers: src/README.md).
 *
 *   @copy WORDS...   one end of a file copy (src/copy/filecopy.h)
 *   @stdio           this program's own standard input and output
 *   @sh COMMAND      a command, run by the user's shell as ssh would run it
 */
namespace Gosh {

/* Whether the words name an application, with words it takes. */
bool stream_words_valid( const std::vector<std::string>& words );

/* What the words name, to be started by Network::stream_child. The words must be valid. */
Network::StreamApplication stream_application( const std::vector<std::string>& words, bool local );

}

#endif
