/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef FILECOPY_HPP
#define FILECOPY_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "src/copy/checksum.h"
#include "src/copy/names.h"
#include "src/copy/options.h"

/*
 * Copying files between the two ends of a session, for gocp (REQ1/CP-1 to REQ1/CP-6).
 *
 * Each end runs one of these, a sender and a receiver, joined by a reliable stream (which the
 * session carries, and keeps through any loss of contact). The sender says what it has; the
 * receiver says which of it it lacks, and how much of each it already holds from an earlier
 * attempt; the sender sends the rest (messages.h). A file arrives under another name and takes its
 * own only when it is whole and its checksum agrees, so a copy that is cut off leaves no broken
 * file, only a part to carry on from. With "automatic" the sender does not finish: it looks again
 * every so often and sends what has changed.
 *
 * This is the edge (design v2, 2.2, copy's "sender, receiver"): the stream, the file system, the
 * clock and the user. What it decides is decisions.h's; the words are options.h's; the bytes are
 * messages.h's; names are names.h's; the checksum is checksum.h's.
 */

namespace FileCopy {

/*
 * Runs one end until the copy is done or the stream ends. Returns an exit status: 0 if everything
 * was copied, 1 if anything went wrong at either end or the stream closed early (run it again to
 * carry on), or the other end sent something that could not be understood.
 */
int run( const Options& options, int in_fd, int out_fd );

}

#endif
