/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The rule is the fork's (mosh-1.4.0-pf.7, src/frontend/stmclient.cc, STMClient::consider_relay
   and set_relay; and src/frontend/mosh-client.cc, where "never" was dropped before set_relay). */

/*
 * relay, start: should the client start its relay now? (REQ1/PATH-1; design v2, 2.2.)
 * Logic: numbers in, yes or no out. The seam asks it each turn of the client's loop.
 */

#ifndef GOSH_RELAY_START_H
#define GOSH_RELAY_START_H

#include <cstdint>
#include <string>

namespace Gosh::Relay {

enum RelayMode
{
  RELAY_NEVER, /* there is no command to start one with */
  RELAY_AUTO,  /* when the server goes quiet */
  RELAY_ALWAYS /* from the start, and nothing by UDP */
};

/*
 * The mode for a word (GOSH_RELAY): "never"; "always"; anything else is auto. And never, whatever
 * the word, if there is no command to start one with. (The fork's mosh-client.cc dropped "never"
 * before set_relay saw it; the two rules are one here.)
 */
inline RelayMode relay_mode( const std::string& word, bool have_command )
{
  if ( !have_command || word == "never" ) {
    return RELAY_NEVER;
  }
  return word == "always" ? RELAY_ALWAYS : RELAY_AUTO;
}

/* Sooner at the start, when a network that will not carry UDP is the likeliest reason. */
static constexpr uint64_t RELAY_AFTER_NO_ANSWER = 3000; /* ms since the session started, the server never heard */
static constexpr uint64_t RELAY_AFTER_QUIET = 10000;    /* ms since the server was last heard */

/*
 * Whether to start the relay at `now` (ms).
 * last_heard: when the server was last heard, or uint64_t(-1) for never (the session's own word for it).
 * engaged: it was started, or tried, already: it is started once.
 * ending: the session is being ended, and a quiet server is no surprise.
 */
inline bool relay_due( uint64_t now, uint64_t started_at, uint64_t last_heard, RelayMode mode, bool engaged, bool ending )
{
  if ( engaged || mode != RELAY_AUTO || ending ) {
    return false;
  }
  return ( last_heard == uint64_t( -1 ) ) ? ( now - started_at > RELAY_AFTER_NO_ANSWER )
                                          : ( now - last_heard > RELAY_AFTER_QUIET );
}

/*
 * A relay that was started has gone: it died, which nobody intended. Whether to start another
 * at `now`: yes, but not within a second of when the last was started, so that one that dies
 * as soon as it starts is not started over and over without a pause; and not while the session
 * is ending. (The fork did not look: a session whose relay died carried nothing from then on
 * and said nothing. docs/FINDINGS.md, 2026-11-13.)
 */
static constexpr uint64_t RELAY_AGAIN_AFTER = 1000; /* ms since the last was started */

inline bool relay_again( uint64_t now, uint64_t last_started, bool ending )
{
  return !ending && now - last_started >= RELAY_AGAIN_AFTER;
}

}

#endif
