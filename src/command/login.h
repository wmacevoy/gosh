/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef GOSH_COMMAND_LOGIN_HPP
#define GOSH_COMMAND_LOGIN_HPP

#include <string>
#include <vector>

#include "src/command/startup.h"

/*
 * the command, login: run ssh, or a local shell, to start the server, and hear its startup message
 * (startup.h); and ask the terminal how many colours it has (REQ1/CMD-1; design v2, 2.2). An edge:
 * processes, pipes, the environment. The fork's (mosh-1.4.0-pf.7, src/frontend/moshwrapper.cc,
 * run), moved.
 */
namespace Gosh {
namespace Command {

/*
 * The terminal's number of colours, as print_colors prints it on standard output, asked of another
 * process (as mosh.pl asked "mosh-client -c": the terminal is set up once, later). "0" if it says
 * something else. Throws Failure if there is no pipe.
 */
std::string terminal_colors( void ( *print_colors )( void ), const std::string& name );

struct Login
{
  bool local;              /* --local: a shell here, not ssh */
  std::vector<std::string> words; /* ssh's words (words.h, login_words); unused for --local */
  bool proxy;              /* ssh's ProxyCommand is this program: run ssh with SHELL=/bin/sh */
  std::string userhost;    /* for --local, said as MOSH IP */
  std::string server_line; /* for --local, run by sh -c */
  std::string server;      /* the server as the user named it, or the default: for a message */
  bool pick_server, port_range;
  bool quiet;              /* nothing on standard output: what the host says goes to standard error */
  std::string name;
  Login()
    : local( false ), words(), proxy( false ), userhost(), server_line(), server(), pick_server( false ),
      port_range( false ), quiet( false ), name()
  {}
};

/*
 * Starts the server and reads what the host says until the startup message is whole or the host
 * stops. What is not the message's (ssh's and the login's chatter) is passed on, to standard error
 * when quiet, else to standard output. startup: begun with the address, if it is already known.
 * Throws Failure: no pipe or process, or a startup message that is not well formed.
 */
void start_server( const Login& login, Startup& startup );

/*
 * What the user's ssh would do for this host: the output of `SSH... -G HOST`, which reads the
 * config and connects nowhere (REQ19/CMD-5; sshconfig.h reads it). Empty if ssh has no -G (older
 * than 6.8), or says nothing, or fails. ssh's standard input is nothing and its standard error is
 * dropped: a config gosh cannot read is a config gosh does as before with.
 */
std::string ssh_config_text( const std::vector<std::string>& ssh, const std::string& host );

}
}

#endif
