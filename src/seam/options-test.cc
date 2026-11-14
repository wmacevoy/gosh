/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/*
 * REQ1/FWD-1, REQ1/FWD-2, REQ1/FWD-5: the forwarding words are taken out of a program's
 * arguments, and everything else reaches the core untouched and in order.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/seam/options.h"

using Words = std::vector<std::string>;

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-72s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

/* Runs the parser and says whether it succeeded; the results are left in the three references. */
/* (In these tests the client is the one given -N, and also the one whose options may come anywhere.) */
static bool take( const Words& args, const char* core_values, bool allow_headless, Gosh::ForwardOptions& o, Words& rest,
                  std::string& error )
{
  return Gosh::take_forward_options( args, core_values, allow_headless, allow_headless, o, rest, error );
}

int main()
{
  Gosh::ForwardOptions o;
  Words rest;
  std::string error;

  /* the client, as the fork's tests start it */
  check( "REQ1/FWD-1: -L and -R with their values in the next word",
         take( { "-L", "8080:h:80", "-R", "9090:g:90", "127.0.0.1", "60001" }, "#", true, o, rest, error )
           && o.tcp == Words{ "8080:h:80" } && o.tcp_remote == Words{ "9090:g:90" } && o.udp.empty()
           && rest == Words{ "127.0.0.1", "60001" } );
  check( "REQ1/FWD-1: a value joined to its letter",
         take( { "-L8080:h:80", "ip", "port" }, "#", true, o, rest, error ) && o.tcp == Words{ "8080:h:80" }
           && rest == Words{ "ip", "port" } );
  check( "REQ1/FWD-2: -LU and -RU take the specification from the word after",
         take( { "-LU", "5353:ns:53", "-RU", "53:ns:5353", "ip", "port" }, "#", true, o, rest, error )
           && o.udp == Words{ "5353:ns:53" } && o.udp_remote == Words{ "53:ns:5353" } && o.tcp.empty()
           && rest == Words{ "ip", "port" } );
  check( "several of a kind are kept in order",
         take( { "-L", "1:a:1", "-L", "2:b:2", "ip", "port" }, "#", true, o, rest, error )
           && o.tcp == Words{ "1:a:1", "2:b:2" } );
  check( "REQ1/FWD-5: -N is the client's, and is taken",
         take( { "-N", "-L", "1:a:1", "ip", "port" }, "#", true, o, rest, error ) && o.headless
           && rest == Words{ "ip", "port" } );
  check( "-N sharing a word with one of the core's letters",
         take( { "-vN", "ip", "port" }, "#", true, o, rest, error ) && o.headless && rest == Words{ "-v", "ip", "port" } );
  check( "-F sets the parity word, which defaults to auto",
         take( { "-F", "always", "ip", "port" }, "#", true, o, rest, error ) && o.parity == "always"
           && take( { "ip", "port" }, "#", true, o, rest, error ) && o.parity == "auto" );
  check( "with nothing of gosh's, the words are untouched and nothing is asked for",
         take( { "-#", "ARGS -L not ours", "-v", "ip", "port" }, "#", true, o, rest, error ) && !o.any() && !o.headless
           && rest == Words{ "-#", "ARGS -L not ours", "-v", "ip", "port" } );
  check( "a value of one of the core's letters is never read as an option",
         take( { "-#", "-L", "ip", "port" }, "#", true, o, rest, error ) && !o.any()
           && rest == Words{ "-#", "-L", "ip", "port" } );

  check( "the client's words may follow the address, as the fork's getopt allowed",
         take( { "127.0.0.1", "60001", "-N", "-L", "1:a:1" }, "#", true, o, rest, error ) && o.headless
           && o.tcp == Words{ "1:a:1" } && rest == Words{ "127.0.0.1", "60001" } );

  /* the server: what follows "new" */
  check( "the server's own options with values pass through, around gosh's",
         take( { "-i", "127.0.0.1", "-L", "1:a:1", "-p", "60001", "-c", "256", "--", "sh", "-c", "x -L y" }, "@ipcl", false,
               o, rest, error )
           && o.tcp == Words{ "1:a:1" }
           && rest == Words{ "-i", "127.0.0.1", "-p", "60001", "-c", "256", "--", "sh", "-c", "x -L y" } );
  check( "after \"--\" nothing is gosh's",
         take( { "--", "-L", "1:a:1" }, "@ipcl", false, o, rest, error ) && !o.any()
           && rest == Words{ "--", "-L", "1:a:1" } );
  check( "-N is not the server's: it is left for the core to refuse",
         take( { "-N" }, "@ipcl", false, o, rest, error ) && !o.headless && rest == Words{ "-N" } );

  /* what cannot be taken */
  check( "-L with nothing after it is an error that names it",
         !take( { "-L" }, "#", true, o, rest, error ) && error == "-L needs a value" );
  check( "-RU with no specification is an error that names it",
         !take( { "-RU" }, "#", true, o, rest, error ) && error == "-RU needs a forwarding specification" );

  /* REQ6/NAME-1: which program it has been asked to be */
  std::string called;
  size_t skip = 9;
  check( "REQ6/NAME-1: under the name gosh it is the command",
         Gosh::which_role( "/home/u/.local/bin/gosh", { "host" }, called, skip ) == Gosh::THE_COMMAND && called == "gosh"
           && skip == 0 );
  check( "REQ6/NAME-1: under the name gocp it is the copy command",
         Gosh::which_role( "gocp", { "a", "host:b" }, called, skip ) == Gosh::THE_COPY && called == "gocp" && skip == 0 );
  check( "with gosh as its first word it is the command, and the word is passed over",
         Gosh::which_role( "./gosh-client", { "gosh", "host" }, called, skip ) == Gosh::THE_COMMAND && called == "gosh"
           && skip == 1 );
  check( "a Windows path and .exe are not part of the name",
         Gosh::which_role( "C:\\bin\\gosh.exe", {}, called, skip ) == Gosh::THE_COMMAND && called == "gosh" );
  check( "under its own name, with an address first, it is the client",
         Gosh::which_role( "/usr/bin/gosh-client", { "127.0.0.1", "60001" }, called, skip ) == Gosh::THE_CLIENT
           && called == "gosh-client" && skip == 0 );
  check( "the names it used to have are not the command (no alias but REQ6/CMD-4's)",
         Gosh::which_role( "mosh", { "host" }, called, skip ) == Gosh::THE_CLIENT
           && Gosh::which_role( "gosh-client", { "mosh", "host" }, called, skip ) == Gosh::THE_CLIENT && skip == 0 );

  if ( failures ) {
    printf( "FAIL REQ1/FWD-1, REQ1/FWD-2, REQ1/FWD-5: %d checks of the forwarding words failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/FWD-1, REQ1/FWD-2, REQ1/FWD-5: the forwarding words\n" );
  return EXIT_SUCCESS;
}
