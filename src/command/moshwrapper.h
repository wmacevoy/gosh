/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef MOSHWRAPPER_HPP
#define MOSHWRAPPER_HPP

#include <string>
#include <vector>

/*
 * What the "mosh" command does, as part of mosh-client itself: log in
 * to the server with ssh, start mosh-server there, read where it is and
 * its key, and become the client of that session.  It was a Perl
 * script (scripts/mosh.pl, which is still there), and that made Perl
 * something to install wherever mosh was wanted, which on Windows is
 * not nothing.  The options are the script's.
 *
 * mosh-client does this when it is run under the name "mosh" or "mocp"
 * (mocp being mosh --copy), or as "mosh-client mosh ..." and
 * "mosh-client mocp ...".
 */

namespace MoshWrapper {

/*
 * args: what followed the command name.  name: what to call ourselves
 * in messages.  self: argv[0] of this process.  print_colors: prints
 * the terminal's number of colors on standard output.
 *
 * Returns true to go on as the client: client_args are then its
 * arguments (the first being its name), and the environment has been
 * set up for it.  Returns false to exit, with status.
 */
bool run( const std::vector<std::string>& args,
          bool copy,
          const std::string& name,
          const std::string& self,
          void ( *print_colors )( void ),
          std::vector<std::string>& client_args,
          int& status );

/* ssh's way to the server when we are its ProxyCommand: says which address it reached, then carries bytes (proxy.cc). */
int fake_proxy( const std::string& family, const std::string& host, const std::string& port );

}

#endif
