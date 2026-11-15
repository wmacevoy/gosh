/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * relay, process: what both relays need as processes, and neither decides anything with: the
 * log, the mark on their words to each other, signals, the clock, writing to a pipe, a UDP
 * socket to an address. An edge; the fork's text (mosh-1.4.0-pf.7, src/network/relay.cc).
 * Included by local.cc and remote.cc and by nothing else: each of them is one process, and has
 * its own copy of what is here.
 */

#ifndef GOSH_RELAY_PROCESS_H
#define GOSH_RELAY_PROCESS_H

#include "src/include/config.h"

#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/pathfinding/punch.h"
#include "src/relay/frame.h"
#include "src/relay/paths.h"
#include "src/relay/relay.h"
#include "src/relay/settings.h"
#include "src/relay/whose.h"

using namespace Network;
using Gosh::relay_frame;
using Gosh::RelayFrameParser;

static const size_t MAX_DATAGRAM = 65535;

/* (gosh) The times that decide a path are in paths.h, with the rules that use them. */
/* how much may wait to go down a stream that is not taking it */
static const size_t STREAM_BACKLOG = 256 * 1024;
/* What the relays are doing, for whoever sets MOSH_RELAY_LOG to a file to hear of it. */
static const char* relay_log_who = "";
static void say( const char* format, ... )
{
  const char* path = getenv( "MOSH_RELAY_LOG" );
  if ( !path || !*path ) {
    return;
  }
  FILE* f = fopen( path, "a" );
  if ( !f ) {
    return;
  }
  fprintf( f, "%s: ", relay_log_who );
  va_list args;
  va_start( args, format );
  vfprintf( f, format, args );
  va_end( args );
  fputc( '\n', f );
  fclose( f );
}

/*
 * Between the relays, on the stream, besides the session's datagrams:
 * frames that begin with the mark and are words for the other relay.
 * (No datagram of mosh's begins so.  A relay from before these existed
 * passes them on as datagrams, to be thrown away for making no sense;
 * and the local relay says nothing of the kind until the remote one
 * has, so an old remote relay is never sent any.)
 *
 *   HELLO 1          remote: I know about these
 *   STUN a:p b:p     local: meetups and STUN servers to ask where you appear to be
 *   KEY k            local: seal probes with this; start
 *   MEETUP a.b.c.d:p local: (gosh) the meetup, found where the ticket was fetched...
 *   TICKET id key    local: ...and the ticket for it: register as end 1 (REQ15/PATH-4)
 *   PORT n           either: the port of my end of the way through
 *   CAND a.b.c.d:p   either: where I may be reached (on my own network, or as seen from outside)
 */
static std::string control( const std::string& text )
{
  return Gosh::relay_words( std::string( PUNCH_MARK, sizeof( PUNCH_MARK ) ), text );
}

static bool is_control( const std::string& frame, std::vector<std::string>& words )
{
  return Gosh::relay_words_of( std::string( PUNCH_MARK, sizeof( PUNCH_MARK ) ), frame, words );
}

static bool is_ipv4( const std::string& text )
{
  Addr addr;
  return parse_ip_port( text + ":1", addr );
}

/*
 * For a process started from the mosh client, which keeps most signals
 * blocked and takes them when it chooses: a new process should not
 * inherit that, or a signal sent to stop it waits for ever.
 */
static void own_signals( void )
{
  sigset_t none;
  sigemptyset( &none );
  sigprocmask( SIG_SETMASK, &none, NULL );
  const int signals[] = { SIGINT, SIGTERM, SIGHUP, SIGCHLD, SIGWINCH, SIGCONT, SIGUSR1, SIGUSR2 };
  for ( size_t i = 0; i < sizeof( signals ) / sizeof( signals[0] ); i++ ) {
    signal( signals[i], SIG_DFL );
  }
}

static uint64_t now_ms( void )
{
  struct timespec t;
#ifdef CLOCK_MONOTONIC
  clock_gettime( CLOCK_MONOTONIC, &t );
#else
  clock_gettime( CLOCK_REALTIME, &t );
#endif
  return static_cast<uint64_t>( t.tv_sec ) * 1000 + t.tv_nsec / 1000000;
}

static bool write_all( int fd, const std::string& s )
{
  size_t done = 0;
  while ( done < s.size() ) {
    ssize_t n = write( fd, s.data() + done, s.size() - done );
    if ( n < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      return false;
    }
    done += n;
  }
  return true;
}

/* A UDP socket for talking to one address and port, or -1. */
static int udp_to( const std::string& address, const char* port )
{
  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  if ( getaddrinfo( address.c_str(), port, &hints, &res ) != 0 ) {
    return -1;
  }
  int fd = socket( res->ai_family, SOCK_DGRAM, 0 );
  if ( fd >= 0 && connect( fd, res->ai_addr, res->ai_addrlen ) != 0 ) {
    close( fd );
    fd = -1;
  }
  freeaddrinfo( res );
  return fd;
}

#endif
