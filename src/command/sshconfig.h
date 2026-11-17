/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef GOSH_COMMAND_SSHCONFIG_H
#define GOSH_COMMAND_SSHCONFIG_H

#include <string>

/*
 * the command, ssh's config: what the user's own ssh would do for a host, as `ssh -G HOST` prints
 * it, and what gosh then does (REQ19/CMD-5; the design, "ssh, gosh's trust provider", job 6: ssh
 * decides reachability). Logic: text in, values out. login.h runs `ssh -G`.
 *
 * Absent (an ssh with no -G, older than 6.8; or one that failed): an empty SshConfig, and gosh
 * does as it did before it read the config. Wrong (lines it does not know): ignored.
 */
namespace Gosh {
namespace Command {

struct SshConfig
{
  std::string hostname;     /* what the host's name is, there: an alias's HostName */
  std::string proxyjump;    /* "" for none */
  std::string proxycommand; /* "" for none */
  SshConfig() : hostname(), proxyjump(), proxycommand() {}
};

/* `ssh -G`'s output: a lower-case keyword, a space, its value, a line each. */
SshConfig read_ssh_config( const std::string& text );

/* Whether a ProxyCommand runs gosh itself (its first word is gosh, gosh-client or gocp, by any path). */
bool runs_gosh( const std::string& proxycommand );

/*
 * How gosh learns the server's address: as the user asked (asked, if not empty), or else
 *   - "remote" (the server says where ssh reached it) when the user's ssh reaches the host its own
 *     way, by a ProxyJump or a ProxyCommand: gosh's own ProxyCommand would take its place, and the
 *     host would not be reached the user's way (found 2026-11-17);
 *   - "proxy", as before, otherwise, and when the user's ProxyCommand is gosh itself: leaving it
 *     would have gosh's login run gosh again, for ever.
 */
std::string remote_ip_for( const std::string& asked, const SshConfig& config );

/*
 * A meetup's address for UDP ("HOST:PORT", HOST bracketed if IPv6), with HOST as the user's ssh
 * would reach it: an alias in ~/.ssh/config becomes its HostName (REQ19/CMD-5). As given when the
 * config says nothing.
 */
std::string meetup_address( const std::string& host_port, const SshConfig& config );

}
}

#endif
