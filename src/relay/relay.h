/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef RELAY_HPP
#define RELAY_HPP

#include <string>
#include <vector>

#include <sys/types.h>

#include "src/network/network.h"

/*
 * A way through for when UDP cannot get there.
 *
 * mosh talks UDP, and some networks will not carry it.  They do carry
 * ssh, or mosh could not have been started at all.  So when datagrams
 * are not getting through, they are carried over an ssh connection
 * instead, between two relays:
 *
 *   mosh-client --UDP--> local relay ==ssh==> remote relay --UDP--> mosh-server
 *    (this machine, loopback)                    (the server, to itself)
 *
 * Nothing changes for mosh-server: it sees a client at one of its own
 * addresses, and when the ssh connection is replaced, a client that has
 * moved, which is what it expects of clients.  Nor does the mosh
 * session depend on the ssh connection, which can come and go.
 *
 * The local relay does not give up on UDP.  Everything from the client
 * is still sent straight to the server as well, and while answers are
 * coming straight back the ssh connection is left alone (and after a
 * while, closed).  It is opened when nothing has come back directly for
 * a few seconds.  Sending both ways costs duplicates, which mosh is
 * indifferent to, and means there is never a decision to get wrong.
 *
 * Over ssh, datagrams lose what made them datagrams: they arrive late
 * rather than never.  It is for a pinch.
 *
 * On the stream each datagram is preceded by its length, as two bytes,
 * most significant first.
 */

namespace Gosh::Relay {

/*
 * The remote relay ("mosh-server relay ADDRESS PORT"): datagrams
 * arriving on standard input go to ADDRESS:PORT, and what comes back
 * goes to standard output, until standard input ends.  Returns an exit
 * status.
 */
int relay_remote( const char* address, const char* port );

struct RelayOptions
{
  Network::Addr server; /* where mosh-server is, for the direct route */
  socklen_t server_len;
  std::vector<std::string> command; /* starts the remote relay, typically through ssh */
  bool stream_only;                 /* don't try UDP at all */

  RelayOptions() : server(), server_len( 0 ), command(), stream_only( false ) {}
};

/*
 * Starts the local relay as a process of its own, and returns the UDP
 * port on 127.0.0.1 that it listens on for the client, or 0 if it could
 * not be started.  It runs until keep_alive_fd is closed, which happens
 * of itself when the caller exits.
 */
int relay_start_local( const RelayOptions& options, int& keep_alive_fd, pid_t& pid );

}

#endif
