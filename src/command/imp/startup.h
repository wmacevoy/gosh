/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef GOSH_COMMAND_STARTUP_HPP
#define GOSH_COMMAND_STARTUP_HPP

#include <string>

/*
 * the command, the startup message (glossary): what the host says when the server starts, read a
 * line at a time: "MOSH IP", "MOSH SSH_CONNECTION", "MOSH CONNECT port key", and which server
 * answered (REQ1/CMD-1, REQ8/CMD-3; design v2, 2.2). Logic: lines in, where the server is and its
 * key out. The fork's (mosh-1.4.0-pf.7, src/frontend/moshwrapper.cc, run), divided.
 *
 * Wrong input: a line that starts as one of these and is not well formed, or a second MOSH IP,
 * throws Failure. Anything else the host says (ssh's and the login's chatter) is not the
 * message's, and is for the caller to pass on.
 */
namespace Gosh::Command::Imp {

struct Startup
{
  std::string ip;     /* the server's address: MOSH IP, or given before the message, for --experimental-remote-ip=local */
  std::string ssh_ip; /* the server's end of $SSH_CONNECTION, if the host said it */
  std::string port, key;
  bool done;     /* MOSH CONNECT has been read: read no more */
  bool stock;    /* (gosh, REQ8/CMD-3) the host said that the server it started is a stock mosh's */
  bool bad_port; /* a port range was asked for, and the server said it does not know one */
  Startup() : ip(), ssh_ip(), port(), key(), done( false ), stock( false ), bad_port( false ) {}
};

/*
 * Reads one line, with its end. True if it was the message's; false if it is the host's own, to be
 * passed on. pick_server: the host was asked to choose the server, so may say which it chose.
 * port_range: a range of ports was asked for. name: what the command calls itself.
 */
bool startup_line( Startup& startup, const std::string& raw, bool pick_server, bool port_range,
                   const std::string& name );

/*
 * Once the host has said all it will, first: the server's address, or a Failure. Returns a line to
 * tell the user when the address is taken from $SSH_CONNECTION, "" otherwise. userhost: as ssh was
 * given it.
 */
std::string startup_address( Startup& startup, const std::string& name, const std::string& userhost );

/* Then: the session's port and key, or a Failure saying why there are none. */
void startup_key( const Startup& startup, const std::string& name );

}

#endif
