/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * path-finding, the mark: the eight bytes that begin a probe (finding.h), and that the relays
 * begin their words to each other with (relay's process.h). The fork's, at mosh-1.4.0-pf.7.
 */

#ifndef GOSH_PATHFINDING_MARK_H
#define GOSH_PATHFINDING_MARK_H

namespace Network {

static const char PUNCH_MARK[8] = { 0, 'M', 'O', 'S', 'H', 'P', 'C', 'H' };

}

#endif
