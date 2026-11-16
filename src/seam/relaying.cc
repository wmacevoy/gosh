/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/seam/session.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/network/network.h"
#include "src/relay/relay.h"

using namespace Gosh;

/* (the fork's STMClient::engage_relay) */
void Gosh::engage_relay( void )
{
  Session& s = session();
  s.relay.engaged = true; /* (or tried: either way, once) */
  s.relay.started_at = Network::timestamp();

  Network::RelayOptions options;
  options.command = s.relay.command;
  options.stream_only = s.relay.mode == RELAY_ALWAYS;

  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  if ( getaddrinfo( s.relay.server_ip.c_str(), s.relay.server_port.c_str(), &hints, &res ) != 0 ) {
    return;
  }
  options.server_len = res->ai_addrlen;
  memcpy( &options.server, res->ai_addr, res->ai_addrlen );
  freeaddrinfo( res );

  pid_t pid;
  const int relay_port = Network::relay_start_local( options, s.relay.keep_alive, pid );
  if ( relay_port == 0 ) {
    return;
  }
  s.relay.pid = pid;

  Network::Addr relay;
  memset( &relay, 0, sizeof( relay ) );
  relay.sin.sin_family = AF_INET;
  relay.sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  relay.sin.sin_port = htons( relay_port );
  s.send_elsewhere( relay, sizeof( relay.sin ) );

  if ( s.options.headless && s.options.stream.empty() ) {
    fputs( s.relay.mode == RELAY_ALWAYS ? "gosh: connecting through ssh.\n"
                                        : "gosh: no answer by UDP; trying through ssh as well.\n",
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
  if ( relay_due( Network::timestamp(), s.started_at, s.last_heard(), s.relay.mode, s.relay.engaged, s.ending() ) ) {
    engage_relay();
  }

  /*
   * Is the relay we started still there? It is our child, so this asks only about it. If it
   * has died we are sending to a port nobody has: say so, and start another (REQ1/PATH-1).
   */
  if ( s.relay.pid > 0 ) {
    const pid_t gone = waitpid( s.relay.pid, NULL, WNOHANG );
    if ( gone == s.relay.pid || ( gone < 0 && errno == ECHILD ) ) {
      s.relay.pid = 0;
      s.relay.gone = true;
      if ( s.relay.keep_alive >= 0 ) {
        close( s.relay.keep_alive );
        s.relay.keep_alive = -1;
      }
      if ( s.options.headless && s.options.stream.empty() && !s.ending() ) {
        fputs( "gosh: the relay through ssh has stopped; starting another.\n", stderr );
        fflush( stderr );
      }
    }
  }
  if ( s.relay.gone && relay_again( Network::timestamp(), s.relay.started_at, s.ending() ) ) {
    engage_relay();
    s.relay.gone = s.relay.pid <= 0; /* (if it could not be started, try again in a second) */
  }
}
