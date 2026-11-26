/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef GOSH_SEAM_HOOKS_HPP
#define GOSH_SEAM_HOOKS_HPP

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <sys/types.h>

#include "src/forwarding/forwarding.h"
#include "src/seam/session.h"

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
 *   loop_returned     (the client) its loop has come to its end
 *   stream_exit       (the client, for a stream) the status to exit with
 *
 * A stream (REQ1/CP-7): the session exists to join a process at each end. The server's is
 * started by forwarding when the stream's connection arrives. The client's is started here once
 * the session is up; when it has ended and the last of what it sent has gone, the client ends
 * the session, and exits with that process's status.
 */
namespace Gosh {

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
  const bool stream = !s.options.stream.empty();
  take_stream_words();
  if ( s.client ) {
    s.ending = [&network]() { return network.shutdown_in_progress(); };
    s.end_it = [&network]() { network.start_shutdown(); };
    s.last_heard = [&network]() { return network.last_heard(); };
    s.send_elsewhere = [&network]( const Network::Addr& to, socklen_t len ) { network.set_remote_addr( to, len ); };
    s.started_at = Network::timestamp();
    if ( s.relay.mode == Gosh::Relay::RELAY_ALWAYS ) {
      engage_relay();
    }
  }
  if ( !s.options.any() && !stream ) {
    return;
  }
  s.forwarding.reset( new Gosh::Forwarding::Forwarding(
    &network, s.client, s.options.udp, s.options.tcp, s.options.tcp_remote, s.options.udp_remote, stream ) );
  if ( stream && !s.client ) {
    serve_stream();
  }
  s.forwarding->set_verbose( verbose );
  if ( !s.forwarding->set_fragment_parity( s.options.parity ) ) {
    throw Network::NetworkException(
      "Bad fragment parity mode (" + s.options.parity + "): expected auto, always or never", 0 );
  }
  network.set_sidechannel_sink( s.forwarding->sink(), stream ? Network::CAPABILITY_STREAM : 0 );
}

/* How long the program's loop may wait: `otherwise`, or less if forwarding has a timer due sooner (ms). */
inline int wait_time( int otherwise )
{
  Session& s = session();
  if ( !s.forwarding ) {
    return otherwise;
  }
  if ( s.client && !s.stream.words.empty() ) {
    otherwise = std::min( 50, otherwise ); /* to see the stream's process end */
  }
  return std::min( otherwise, s.forwarding->wait_time() );
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
  if ( s.client ) {
    consider_relay();
  }
  if ( !s.forwarding ) {
    return;
  }
  for ( std::vector<int>::const_iterator it = s.watched.begin(); it != s.watched.end(); it++ ) {
    if ( sel.read( *it ) ) {
      s.forwarding->read_fd( *it );
    }
  }
  s.forwarding->tick();
  if ( s.client && !s.stream.words.empty() ) {
    tend_stream();
  }
}

/*
 * A headless client has no screen to draw. Once, when the session is up and its forwards can
 * be used, it says so on standard error, where a script can wait for it (REQ1/FWD-5); or, once,
 * that the server it reached does not forward (REQ8/CMD-3).
 * still_connecting: the client has not yet heard from the server.
 */
template<class Transport>
void announce( Transport& network, bool still_connecting )
{
  Session& s = session();
  if ( s.announced || still_connecting ) {
    return;
  }
  const bool stream = !s.stream.words.empty();
  if ( stream && !network.peer_can( Network::CAPABILITY_STREAM ) ) {
    /* (REQ8/CMD-3) It has answered, and has not said that it can do what we are here for. */
    s.announced = true;
    fputs( "gosh: the server does not do this: it is a stock mosh-server, or one from before streams.\n", stderr );
    fflush( stderr );
    stream_never_ran();
    return;
  }
  if ( !stream && s.forwarding && !network.peer_can( Network::CAPABILITY_SIDECHANNEL ) ) {
    /*
     * (REQ8/CMD-3) The server has answered and has not said that it forwards: a stock mosh, or
     * one of gosh's that was not given these forwards. Said once and plainly, because whoever
     * waits for "connected; forwarding" would otherwise wait for ever.
     */
    s.announced = true;
    fputs( "gosh: connected, but the server does not forward: it is a stock mosh-server, or was not given\n"
           "      these forwards. None of them will be done.\n",
           stderr );
    fflush( stderr );
    return;
  }
  if ( s.forwarding && !network.sidechannel_ready() ) {
    return; /* it forwards, and is not ready yet */
  }
  s.announced = true;
  if ( stream ) {
    start_stream(); /* the session is up: our end of the stream, and the server asked for its end */
    return;
  }
  fputs( s.forwarding ? "gosh: connected; forwarding.\n" : "gosh: connected.\n", stderr );
  fflush( stderr );
}

/* The client's loop has come to its end (and was not left by an exception). */
inline void loop_returned( void )
{
  session().loop_returned = true;
}

/* Ends forwarding and closes its sockets. Safe to call when there is none. */
inline void session_ended( void )
{
  session().forwarding.reset();
}

}

#endif
