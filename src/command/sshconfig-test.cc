/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ19/CMD-5: what gosh makes of the user's ssh config (sshconfig.h). */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/command/sshconfig.h"

using namespace Gosh::Command;

static int failures = 0;

static void check( const char* what, bool ok, const std::string& got = "" )
{
  printf( "%-76s %s%s\n", what, ok ? "ok" : "FAIL ", ok ? "" : got.c_str() );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  /* as OpenSSH 9.2's ssh -G prints them, in part */
  const SshConfig jump = read_ssh_config( "host inside\nhostname 10.9.9.9\nport 22\nproxyjump bastion.example\n" );
  check( "ssh -G read: the HostName and a ProxyJump", jump.hostname == "10.9.9.9" && jump.proxyjump == "bastion.example"
                                                          && jump.proxycommand.empty() );
  const SshConfig plain = read_ssh_config( "hostname server.example\nproxycommand none\nproxyjump none\n" );
  check( "\"none\" is no proxy", plain.proxyjump.empty() && plain.proxycommand.empty() );
  const SshConfig command = read_ssh_config( "hostname h\nproxycommand nc -X 5 -x socks:1080 %h %p\n" );
  check( "a ProxyCommand, all its words", command.proxycommand == "nc -X 5 -x socks:1080 %h %p" );
  check( "nothing read from nothing (an ssh with no -G)", read_ssh_config( "" ).hostname.empty() );

  check( "with nothing asked and no proxy of the user's, the address by gosh's proxy, as before",
         remote_ip_for( "", plain ) == "proxy" );
  check( "with the user's ProxyJump, the address from the server: the host reached the user's way",
         remote_ip_for( "", jump ) == "remote" );
  check( "and so with the user's own ProxyCommand", remote_ip_for( "", command ) == "remote" );
  const SshConfig itself = read_ssh_config( "hostname h\nproxycommand /home/u/.local/bin/gosh -W localhost:22 %h\n" );
  check( "but not when the user's ProxyCommand is gosh itself: its login would run gosh again",
         runs_gosh( itself.proxycommand ) && remote_ip_for( "", itself ) == "proxy" );
  check( "gosh-client and gocp are gosh too; goshawk is not",
         runs_gosh( "gosh-client x" ) && runs_gosh( "  gocp" ) && !runs_gosh( "goshawk %h" ) && !runs_gosh( "" ) );
  check( "what the user asked is what is done", remote_ip_for( "local", jump ) == "local" && remote_ip_for( "proxy", jump ) == "proxy" );

  const SshConfig alias = read_ssh_config( "hostname m.example.org\n" );
  check( "a meetup named by an alias is reached at the alias's HostName", meetup_address( "mymeetup:3478", alias ) == "m.example.org:3478",
         meetup_address( "mymeetup:3478", alias ) );
  const SshConfig six = read_ssh_config( "hostname 2001:db8::9\n" );
  check( "an IPv6 HostName, bracketed", meetup_address( "m6:3478", six ) == "[2001:db8::9]:3478", meetup_address( "m6:3478", six ) );
  check( "as given when the config says nothing", meetup_address( "m:3478", SshConfig() ) == "m:3478" );

  if ( failures ) {
    printf( "FAIL REQ19/CMD-5: %d checks failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ19/CMD-5: the user's ssh config, read and followed\n" );
  return EXIT_SUCCESS;
}
