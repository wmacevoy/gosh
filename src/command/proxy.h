/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef GOSH_COMMAND_PROXY_HPP
#define GOSH_COMMAND_PROXY_HPP

#include <string>
#include <vector>

#include <sys/socket.h>

/*
 * the command, proxy: ssh's way to the server when this program is its ProxyCommand: reach the
 * host, say which address was reached, and carry bytes (REQ1/CMD-1; design v2, 2.2). An edge:
 * names looked up, a socket, and this program's own path. The fork's (mosh-1.4.0-pf.7,
 * src/frontend/moshwrapper.cc), moved.
 */
namespace Gosh {
namespace Command {

/* An address of a host, as --family would have it. */
struct Address
{
  int family;
  struct sockaddr_storage addr;
  socklen_t len;
  std::string text;
  Address() : family( 0 ), addr(), len( 0 ), text() {}
};

/*
 * The addresses of a host, for this port, in the order --family wants them ("prefer-..." puts that
 * family first; "auto" refuses a host with both). Throws Failure: the host cannot be resolved, or
 * "auto" and both families. name: what the command calls itself.
 */
std::vector<Address> resolve( const std::string& host, const std::string& port, const std::string& family,
                              const std::string& name );

/* Where this program is, for ssh to run it again as its ProxyCommand. argv0: as it was run. */
std::string self_path( const std::string& argv0 );

/* That path as something ssh can be given to run, with the family, %h and %p. */
std::string proxy_command( const std::string& self, const std::string& family );

}
}

#endif
