/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING). gosh carries mosh's code
    and is not the mosh project's work. The words parsed here, and what they
    mean, are those of wmacevoy/mosh at mosh-1.4.0-pf.7.
*/

#ifndef GOSH_SEAM_OPTIONS_HPP
#define GOSH_SEAM_OPTIONS_HPP

#include <string>
#include <vector>

/*
 * The seam: the words gosh adds to the two programs' arguments, taken out of
 * them before the core's own main reads what is left (design v2, 4.2).
 *
 * Logic only: values in, values out. No socket, clock, environment or output.
 */
namespace Gosh {

/* What a program was asked to forward (REQ1/FWD-1, REQ1/FWD-2, REQ1/FWD-5). */
struct ForwardOptions
{
  std::vector<std::string> tcp;        /* -L [BIND:]PORT:HOST:HOSTPORT, as ssh's */
  std::vector<std::string> tcp_remote; /* -R */
  std::vector<std::string> udp;        /* -LU */
  std::vector<std::string> udp_remote; /* -RU */
  std::string parity;                  /* -F auto|always|never: parity for fragmented UDP */
  bool headless;                       /* -N: no terminal (the client only) */

  ForwardOptions() : tcp(), tcp_remote(), udp(), udp_remote(), parity( "auto" ), headless( false ) {}

  /* Whether any forward was asked for. */
  bool any( void ) const { return !tcp.empty() || !tcp_remote.empty() || !udp.empty() || !udp_remote.empty(); }
};

/*
 * Takes gosh's forwarding words out of a program's arguments.
 *
 *   args            what followed the program's name (for the server, what followed "new")
 *   core_values     the core's own option letters that are followed by a value ("#" for the
 *                   client, "@ipcl" for the server), so that a value is not read as an option
 *   allow_headless  whether -N is gosh's here (the client), or left for the core to refuse
 *
 * Options are read as getopt reads them, up to "--" or the first word that is not an option:
 * letters may share a word ("-vN"), and a value may be joined to its letter ("-L8080:h:80") or be
 * the next word. "-LU SPEC" and "-RU SPEC" are -L and -R with the value "U" and the
 * specification in the word after.
 *
 * Returns true with `options` filled and `rest` holding every word that was not gosh's, in
 * order, for the core. Returns false with `error` saying which word lacks its value; `options`
 * and `rest` are then not to be used. It does not check that a specification makes sense:
 * forwarding does, when it is given them.
 */
bool take_forward_options( const std::vector<std::string>& args,
                           const std::string& core_values,
                           bool allow_headless,
                           ForwardOptions& options,
                           std::vector<std::string>& rest,
                           std::string& error );

/* Which program gosh-client has been asked to be (REQ6/NAME-1). */
enum Role
{
  THE_CLIENT, /* the client of a session someone else started */
  THE_COMMAND, /* gosh: log in, start the server, and become the client */
  THE_COPY     /* gocp: the command, copying files */
};

/*
 * gosh-client is the command when it is run under the name gosh or gocp, or with either as its
 * first word ("gosh-client gosh host"), which is how it is used where a second name cannot be
 * made. The name is what follows the last "/" or "\\" of `program`, less ".exe".
 *
 *   program  argv[0], as the program was started
 *   args     what followed it
 *
 * Returns the role. `called` is then the name to use in messages, and `skip` how many of `args`
 * were the name and are to be passed over (0 or 1).
 */
Role which_role( const std::string& program, const std::vector<std::string>& args, std::string& called, size_t& skip );

}

#endif
