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
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <sys/types.h>

#include "src/forwarding/forwarding.h"
#include "src/forwarding/streamchild.h"
#include "src/seam/options.h"
#include "src/relay/start.h"

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

/* The one session of gosh's in this process. Set by gosh's main before the core's main runs. */
struct Session
{
  ForwardOptions options; /* what the program was asked to forward */
  bool client;            /* which end this is */
  std::unique_ptr<Network::Forwarding> forwarding;
  std::vector<int> watched; /* forwarding's descriptors, as last given to the program's loop */
  bool announced;           /* whether a headless client has said that it is connected */

  /* a stream: the process at this end (the client's; the server's is forwarding's to start) */
  std::vector<std::string> stream_words; /* what to start; empty if the session is not for a stream */
  pid_t stream_pid;                      /* 0 before it starts, -1 once it has ended or could not start */
  int stream_status;                     /* how it ended, as an exit status */
  uint64_t stream_ended;                 /* when */
  /* the session's own state, as the client's loop cannot be handed it each pass (set by session_started) */
  std::function<bool()> ending;         /* the session is being ended */
  std::function<void()> end_it;         /* begin to end it */
  bool loop_returned;                   /* the client's loop came to its end, and was not thrown out of */

  /* the relay (the client's): whether, with what, to where; and whether it was started (REQ1/PATH-1) */
  RelayMode relay_mode;
  std::vector<std::string> relay_command; /* starts the remote relay, through ssh as a rule */
  std::string server_ip, server_port;     /* where the server is, as the client was told */
  bool relay_engaged;
  int relay_keep_alive; /* the local relay runs until this closes, which it does when we exit */
  uint64_t started_at;
  std::function<uint64_t()> last_heard; /* when the server was last heard (set by session_started) */
  std::function<void( const Network::Addr&, socklen_t )> send_elsewhere; /* send to here from now on */

  Session()
    : options(), client( false ), forwarding(), watched(), announced( false ), stream_words(), stream_pid( 0 ),
      stream_status( 1 ), stream_ended( 0 ), ending(), end_it(), loop_returned( false ), relay_mode( RELAY_NEVER ),
      relay_command(), server_ip(), server_port(), relay_engaged( false ), relay_keep_alive( -1 ), started_at( 0 ),
      last_heard(), send_elsewhere()
  {}
};

Session& session( void );

/* The client, for a stream: sees whether its process has ended, and ends the session when the stream is over. */
void tend_stream( void );

/* The client: starts the local relay, and talks to the server through it from now on. Once. */
void engage_relay( void );

/* The client: if the server has gone quiet and there is a relay to start, starts it (start.h has the rule). */
void consider_relay( void );

/* Whether this program may yet start a process: a stream's, or the relay. (It must not give up the right to.) */
inline bool may_start_processes( void )
{
  return !session().options.stream.empty() || session().relay_mode != RELAY_NEVER;
}

/* Whether this session is for a stream. */
inline bool has_stream( void )
{
  return !session().options.stream.empty();
}

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
  const bool stream = !s.options.stream.empty();
  /* (only then: the words of an empty string are one empty word, which is not "no stream") */
  s.stream_words = stream ? Network::stream_words( s.options.stream ) : std::vector<std::string>();
  if ( s.client ) {
    s.ending = [&network]() { return network.shutdown_in_progress(); };
    s.end_it = [&network]() { network.start_shutdown(); };
    s.last_heard = [&network]() { return network.last_heard(); };
    s.send_elsewhere = [&network]( const Network::Addr& to, socklen_t len ) { network.set_remote_addr( to, len ); };
    s.started_at = Network::timestamp();
    if ( s.relay_mode == RELAY_ALWAYS ) {
      engage_relay();
    }
  }
  if ( !s.options.any() && !stream ) {
    return;
  }
  s.forwarding.reset( new Network::Forwarding(
    &network, s.client, s.options.udp, s.options.tcp, s.options.tcp_remote, s.options.udp_remote, stream ) );
  if ( stream && !s.client ) {
    /* the server's end of the stream is started when the client asks for it */
    const std::vector<std::string> words = s.stream_words;
    s.forwarding->set_stream_spawner( [words]() { return Network::stream_child( words, false, NULL ); } );
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
  if ( s.client && !s.stream_words.empty() ) {
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
  if ( s.client && !s.stream_words.empty() ) {
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
  const bool stream = !s.stream_words.empty();
  if ( stream && !network.peer_can( Network::CAPABILITY_STREAM ) ) {
    /* (REQ8/CMD-3) It has answered, and has not said that it can do what we are here for. */
    s.announced = true;
    fputs( "mosh: the server does not do this: it is a stock mosh-server, or one from before streams.\n", stderr );
    fflush( stderr );
    s.stream_pid = -1;
    s.stream_ended = Network::timestamp();
    return;
  }
  if ( !stream && s.forwarding && !network.peer_can( Network::CAPABILITY_SIDECHANNEL ) ) {
    /*
     * (REQ8/CMD-3) The server has answered and has not said that it forwards: a stock mosh, or
     * one of gosh's that was not given these forwards. Said once and plainly, because whoever
     * waits for "connected; forwarding" would otherwise wait for ever.
     */
    s.announced = true;
    fputs( "mosh: connected, but the server does not forward: it is a stock mosh-server, or was not given\n"
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
    /* The session is up: start our end of the stream, and ask the server for its end. */
    pid_t pid = 0;
    const int fd = Network::stream_child( s.stream_words, true, &pid );
    if ( fd < 0 ) {
      s.stream_pid = -1;
      s.stream_ended = Network::timestamp();
    } else {
      s.stream_pid = pid;
      s.forwarding->open_stream( fd ); /* (if it can't be, the process sees its stream end at once) */
    }
    return;
  }
  fputs( s.forwarding ? "mosh: connected; forwarding.\n" : "mosh: connected.\n", stderr );
  fflush( stderr );
}

/* The client's loop has come to its end (and was not left by an exception). */
inline void loop_returned( void )
{
  session().loop_returned = true;
}

/*
 * The client, for a stream: the status to exit with. We were only the means; what matters is
 * how the process the stream was for got on: its exit status; or 1 if it never ran, or if the
 * client's loop was thrown out of before it could be asked.
 * success: whether the session itself ended cleanly.
 */
inline int stream_exit( bool success )
{
  Session& s = session();
  const int status = ( s.loop_returned && s.stream_pid == -1 ) ? s.stream_status : 1;
  if ( status == 1 && !success ) {
    fputs( "mosh: the session ended before its work was done.\n", stderr );
  }
  return status;
}

/* Ends forwarding and closes its sockets. Safe to call when there is none. */
inline void session_ended( void )
{
  session().forwarding.reset();
}

}

#endif
