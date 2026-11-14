/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING). gosh carries mosh's code
    and is not the mosh project's work.
*/

#ifndef GOSH_COMMAND_STOCK_HPP
#define GOSH_COMMAND_STOCK_HPP

#include <string>
#include <vector>

/*
 * The command, when the host's server may be a stock mosh's (REQ8/CMD-3):
 * "gosh asks a host for gosh-server, and for mosh-server where there is none. When what it got
 * is not gosh's, it says so once; it asks that server for no forward, stream or relay; and for
 * each of those the user asked for, it says that it will not be done."
 *
 * Logic only: words in, words out. The command (moshwrapper.cc) does the asking and the saying.
 */
namespace Gosh {

/* The line the host prints, before the server's own startup message, when the server is not gosh's. */
extern const char STOCK_SERVER_LINE[];

/*
 * The command line, for the host's shell, that starts whichever server the host has.
 *
 *   gosh_words   gosh-server's arguments, already quoted for a shell
 *   stock_words  mosh-server's: the same less everything only gosh-server knows
 *
 * It looks in ~/.local/bin first, where gosh installs without root and where a shell started by
 * ssh often does not look. If gosh-server is there or on the PATH it becomes that; otherwise it
 * prints STOCK_SERVER_LINE on a line of its own and becomes mosh-server. If the host has
 * neither, the shell says so and nothing more is printed: the caller then finds no startup message.
 */
std::string server_command( const std::string& gosh_words, const std::string& stock_words );

/*
 * What gosh says, once, when the server it got is a stock mosh's: which it got, and each thing
 * the user asked for that will therefore not be done.
 *
 *   name      what the command calls itself in messages
 *   host      the host, as the user gave it
 *   forwards  the forwarding words as they would have been passed: an option (-L, -R, -LU, -RU,
 *             or -F, which is passed over) and then its value, in pairs
 *   relay     whether the user asked for the ssh fallback
 *
 * One line, with no newline at its end.
 */
std::string stock_notice( const std::string& name, const std::string& host, const std::vector<std::string>& forwards,
                          bool relay );

}

#endif
