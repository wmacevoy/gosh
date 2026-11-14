/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

#include "src/seam/hooks.h"

#include <cerrno>

#include <sys/wait.h>

#include "src/forwarding/stream/stream.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "src/network/network.h"
#include "src/relay/relay.h"

/*
 * The one process-wide value the seam has (design v2, 4.2). It rests on "one mosh session per
 * process", which is true of both programs. It is made on first use, so its order among other
 * statics does not matter.
 */
Gosh::Session& Gosh::session( void )
{
  static Session the_session;
  return the_session;
}

/* Has the process at our end of the stream finished? Then, soon, so have we (forwarding/stream.h says when). */
void Gosh::tend_stream( void )
{
  Session& s = session();
  if ( s.stream_pid > 0 ) {
    int status = 0;
    const pid_t waited = waitpid( s.stream_pid, &status, WNOHANG );
    if ( waited == s.stream_pid ) {
      s.stream_status = WIFEXITED( status ) ? WEXITSTATUS( status ) : 128 + WTERMSIG( status );
      s.stream_pid = -1;
      s.stream_ended = Network::timestamp();
    } else if ( waited < 0 && errno == ECHILD ) {
      /*
       * There is no such child to wait for: whoever started us had child signals ignored, so the
       * system has taken it away already, and with it how it ended. It has ended; without this
       * we would wait for ever. (The fork does wait for ever.) Its status is not known: 1.
       */
      s.stream_status = 1;
      s.stream_pid = -1;
      s.stream_ended = Network::timestamp();
    }
  }
  if ( s.forwarding && s.ending && s.end_it
       && stream_over( s.stream_pid == -1, s.stream_ended, s.forwarding->stream_connections(), Network::timestamp(),
                       s.ending() ) ) {
    s.end_it();
  }
}

/* (the fork's STMClient::engage_relay) */
void Gosh::engage_relay( void )
{
  Session& s = session();
  s.relay_engaged = true; /* (or tried: either way, once) */

  Network::RelayOptions options;
  options.command = s.relay_command;
  options.stream_only = s.relay_mode == RELAY_ALWAYS;

  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  if ( getaddrinfo( s.server_ip.c_str(), s.server_port.c_str(), &hints, &res ) != 0 ) {
    return;
  }
  options.server_len = res->ai_addrlen;
  memcpy( &options.server, res->ai_addr, res->ai_addrlen );
  freeaddrinfo( res );

  /*
   * Path-finding is not landed (queue item 8): the relay is started with it off, whatever the
   * environment says, so that nothing untested runs and no server on the Internet is asked
   * anything. This line goes when item 8 lands.
   */
  setenv( "MOSH_PUNCH", "never", 1 );

  pid_t pid;
  const int relay_port = Network::relay_start_local( options, s.relay_keep_alive, pid );
  if ( relay_port == 0 ) {
    return;
  }

  Network::Addr relay;
  memset( &relay, 0, sizeof( relay ) );
  relay.sin.sin_family = AF_INET;
  relay.sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  relay.sin.sin_port = htons( relay_port );
  s.send_elsewhere( relay, sizeof( relay.sin ) );

  if ( s.options.headless && s.options.stream.empty() ) {
    fputs( s.relay_mode == RELAY_ALWAYS ? "mosh: connecting through ssh.\n"
                                        : "mosh: no answer by UDP; trying through ssh as well.\n",
           stderr );
    fflush( stderr );
  }
}

void Gosh::consider_relay( void )
{
  Session& s = session();
  if ( !s.last_heard ) {
    return; /* the session has not started */
  }
  if ( relay_due( Network::timestamp(), s.started_at, s.last_heard(), s.relay_mode, s.relay_engaged, s.ending() ) ) {
    engage_relay();
  }
}
