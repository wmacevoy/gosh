/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING). gosh carries mosh's code
    and is not the mosh project's work. What these hooks do, and the messages
    they print, are those of wmacevoy/mosh at mosh-1.4.0-pf.7, where the same
    lines stood inside the core's two programs.
*/

#ifndef GOSH_SEAM_HOOKS_HPP
#define GOSH_SEAM_HOOKS_HPP

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "src/forwarding/forwarding.h"
#include "src/seam/options.h"

/*
 * The seam: what the core's two programs call, at the places the seam's patches
 * name, to give gosh's parts their turn (design v2, 4.1, rows 6 to 8).
 *
 * This is an edge. It owns the process's one session of gosh's (a program holds
 * exactly one mosh session), and through forwarding it owns sockets. The
 * functions are templates only because the core's transport and its select
 * helper are; each is a few lines, and none decides anything a part should.
 *
 * The order the core calls them in:
 *   session_started   once, when the session's transport exists
 *   in each pass of the program's loop:
 *     wait_time       before it waits: no longer than forwarding's next timer
 *     watch           before it waits: forwarding's descriptors as well
 *     turn            after it waits: what became readable, and the timers
 *   announce          (the client, headless) in place of drawing the screen
 *   session_ended     before the transport goes, or in a child that must not hold its sockets
 */
namespace Gosh {

/* The one session of gosh's in this process. Set by gosh's main before the core's main runs. */
struct Session
{
  ForwardOptions options; /* what the program was asked to forward */
  bool client;            /* which end this is */
  std::unique_ptr<Network::Forwarding> forwarding;
  std::vector<int> watched; /* forwarding's descriptors, as last given to the program's loop */
  bool announced;           /* whether a headless client has said that it is connected */

  Session() : options(), client( false ), forwarding(), watched(), announced( false ) {}
};

Session& session( void );

/* Whether this client has no terminal (REQ1/FWD-5). */
inline bool headless( void )
{
  return session().options.headless;
}

/*
 * Starts forwarding, if any was asked for, and gives it the session's side channel.
 * Throws Network::NetworkException if a specification cannot be parsed, resolved or bound, or
 * the parity word is not one of auto, always, never: said now, while the program can still
 * complain to whoever started it.
 */
template<class Transport>
void session_started( Transport& network, unsigned int verbose )
{
  Session& s = session();
  if ( !s.options.any() ) {
    return;
  }
  s.forwarding.reset( new Network::Forwarding(
    &network, s.client, s.options.udp, s.options.tcp, s.options.tcp_remote, s.options.udp_remote ) );
  s.forwarding->set_verbose( verbose );
  if ( !s.forwarding->set_fragment_parity( s.options.parity ) ) {
    throw Network::NetworkException(
      "Bad fragment parity mode (" + s.options.parity + "): expected auto, always or never", 0 );
  }
  network.set_sidechannel_sink( s.forwarding->sink() );
}

/* How long the program's loop may wait: `otherwise`, or less if forwarding has a timer due sooner (ms). */
inline int wait_time( int otherwise )
{
  Session& s = session();
  return s.forwarding ? std::min( otherwise, s.forwarding->wait_time() ) : otherwise;
}

/* Adds forwarding's descriptors to those the program's loop waits on. */
template<class Select>
void watch( Select& sel )
{
  Session& s = session();
  s.watched = s.forwarding ? s.forwarding->fds() : std::vector<int>();
  for ( std::vector<int>::const_iterator it = s.watched.begin(); it != s.watched.end(); it++ ) {
    sel.add_fd( *it );
  }
}

/* After the wait: lets forwarding read what became readable, and run its timers. */
template<class Select>
void turn( Select& sel )
{
  Session& s = session();
  if ( !s.forwarding ) {
    return;
  }
  for ( std::vector<int>::const_iterator it = s.watched.begin(); it != s.watched.end(); it++ ) {
    if ( sel.read( *it ) ) {
      s.forwarding->read_fd( *it );
    }
  }
  s.forwarding->tick();
}

/*
 * A headless client has no screen to draw. Once, when the session is up and its forwards can
 * be used, it says so on standard error, where a script can wait for it (REQ1/FWD-5).
 * still_connecting: the client has not yet heard from the server.
 */
template<class Transport>
void announce( Transport& network, bool still_connecting )
{
  Session& s = session();
  if ( s.announced || still_connecting || ( s.forwarding && !network.sidechannel_ready() ) ) {
    return;
  }
  s.announced = true;
  fputs( s.forwarding ? "mosh: connected; forwarding.\n" : "mosh: connected.\n", stderr );
  fflush( stderr );
}

/* Ends forwarding and closes its sockets. Safe to call when there is none. */
inline void session_ended( void )
{
  session().forwarding.reset();
}

}

#endif
