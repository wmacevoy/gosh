/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ19/CMD-5: what gosh makes of the user's ssh config (sshconfig.h). */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/command/sshconfig.h"
#include "vendor/facts/facts.h"

using namespace Gosh::Command;
/* REQ19/CMD-5: the user's ssh config, read and followed */
FACTS( Sshconfig )
{
  /* as OpenSSH 9.2's ssh -G prints them, in part */
  const SshConfig jump = read_ssh_config( "host inside\nhostname 10.9.9.9\nport 22\nproxyjump bastion.example\n" );
  FACT_CHECK( ( jump.hostname == "10.9.9.9" && jump.proxyjump == "bastion.example"
                                                          && jump.proxycommand.empty() ), "ssh -G read: the HostName and a ProxyJump" );
  const SshConfig plain = read_ssh_config( "hostname server.example\nproxycommand none\nproxyjump none\n" );
  FACT_CHECK( ( plain.proxyjump.empty() && plain.proxycommand.empty() ), "\"none\" is no proxy" );
  const SshConfig command = read_ssh_config( "hostname h\nproxycommand nc -X 5 -x socks:1080 %h %p\n" );
  FACT_CHECK( command.proxycommand == "nc -X 5 -x socks:1080 %h %p", "a ProxyCommand, all its words" );
  FACT_CHECK( read_ssh_config( "" ).hostname.empty(), "nothing read from nothing (an ssh with no -G)" );

  FACT_CHECK( remote_ip_for( "", plain ) == "proxy", "with nothing asked and no proxy of the user's, the address by gosh's proxy, as before" );
  FACT_CHECK( remote_ip_for( "", jump ) == "remote", "with the user's ProxyJump, the address from the server: the host reached the user's way" );
  FACT_CHECK( remote_ip_for( "", command ) == "remote", "and so with the user's own ProxyCommand" );
  const SshConfig itself = read_ssh_config( "hostname h\nproxycommand /home/u/.local/bin/gosh -W localhost:22 %h\n" );
  FACT_CHECK( ( runs_gosh( itself.proxycommand ) && remote_ip_for( "", itself ) == "proxy" ), "but not when the user's ProxyCommand is gosh itself: its login would run gosh again" );
  FACT_CHECK( ( runs_gosh( "gosh-client x" ) && runs_gosh( "  gocp" ) && !runs_gosh( "goshawk %h" ) && !runs_gosh( "" ) ), "gosh-client and gocp are gosh too; goshawk is not" );
  FACT_CHECK( ( remote_ip_for( "local", jump ) == "local" && remote_ip_for( "proxy", jump ) == "proxy" ), "what the user asked is what is done" );

  const SshConfig alias = read_ssh_config( "hostname m.example.org\n" );
  FACT_CHECK( meetup_address( "mymeetup:3478", alias ) == "m.example.org:3478", "a meetup named by an alias is reached at the alias's HostName" << ": " << ( meetup_address( "mymeetup:3478", alias ) ) );
  const SshConfig six = read_ssh_config( "hostname 2001:db8::9\n" );
  FACT_CHECK( meetup_address( "m6:3478", six ) == "[2001:db8::9]:3478", "an IPv6 HostName, bracketed" << ": " << ( meetup_address( "m6:3478", six ) ) );
  FACT_CHECK( meetup_address( "m:3478", SshConfig() ) == "m:3478", "as given when the config says nothing" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Sshconfig );
}

FACTS_MAIN
