/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * (gosh) forward: the specification of one forward, [bind_address:]port:host:hostport (REQ1/FWD-1, REQ1/FWD-4).
 * The fork's text at mosh-1.4.0-pf.7, from src/network/udpforward.{h,cc}, moved here
 * under its own name and into gosh's namespace; nothing else changed. Logic: no socket, no clock.
 */

#ifndef GOSH_FORWARD_H
#define GOSH_FORWARD_H

#include <string>

namespace Gosh::Forwarding::Imp {

/* [bind_address:]port:host:hostport -- IPv6 addresses go in [brackets]. */
class ForwardSpec
{
public:
  std::string bind_host; /* empty or "*" means all interfaces; default is loopback */
  std::string bind_port;
  std::string target_host;
  std::string target_port;

  ForwardSpec( const std::string& spec ); /* throws Network::NetworkException */
};

}

#endif
