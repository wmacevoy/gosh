/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, master: the meetup's one secret, its master key, in a file in gosh's directory
 * (the proposal, revision 10, section 3). An edge: the environment, the file system and
 * randomness.
 *
 * gosh's directory is $XDG_CONFIG_HOME/gosh, or ~/.config/gosh, on Linux and BSD, and
 * ~/Library/Application Support/gosh on macOS. The key is the file meetup.key there: 16 bytes,
 * readable and writable by its owner alone.
 *
 * Absent: made, the first time it is asked for. Wrong: a file of any other size, or one that
 * anyone else may read or write, is refused, saying why, and never replaced. A new key (`--new`)
 * is written whole beside the old one and then put in its place, so the file is never half
 * written; every ticket made under the old one is then no good.
 */

#ifndef GOSH_MEETUP_MASTER_H
#define GOSH_MEETUP_MASTER_H

#include <string>

namespace Gosh {

/* gosh's directory; empty, and why, if HOME and XDG_CONFIG_HOME are both unset. */
std::string gosh_directory( std::string& error );

/* The master key: read, or made if there is none. False, and why, if it cannot be. */
bool meetup_master( std::string& master, std::string& error );

/* A new master key, in place of the old. False, and why, if it cannot be written. */
bool meetup_master_new( std::string& error );

}

#endif
