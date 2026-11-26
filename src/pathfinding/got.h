/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * path-finding, what came: what a datagram taken from the socket was, as Punch::receive says it
 * (punch.h), and as the logic decides it (imp/finding.h).
 */

#ifndef GOSH_PATHFINDING_GOT_H
#define GOSH_PATHFINDING_GOT_H

namespace Gosh::Pathfinding {

enum Got
{
  NOTHING, /* nothing there to read (the edge's to say) */
  OTHER,   /* something of ours, or of nobody's: dealt with */
  DATA,    /* a datagram of the session's */
  GOODBYE  /* the other end has finished */
};

}

#endif
