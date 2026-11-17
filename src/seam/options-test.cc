/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/FWD-1, REQ1/FWD-2, REQ1/FWD-5: the forwarding words are taken out of a program's
 * arguments, and everything else reaches the core untouched and in order.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/seam/options.h"
#include "vendor/facts/facts.h"

using Words = std::vector<std::string>;



/* Runs the parser and says whether it succeeded; the results are left in the three references. */
/* (In these tests the client is the one given -N, and also the one whose options may come anywhere.) */
static bool take( const Words& args, const char* core_values, bool allow_headless, Gosh::ForwardOptions& o, Words& rest,
                  std::string& error )
{
  return Gosh::take_forward_options( args, core_values, allow_headless, allow_headless, o, rest, error );
}
/* REQ1/FWD-1, REQ1/FWD-2, REQ1/FWD-5: the forwarding words */
FACTS( Options )
{
  Gosh::ForwardOptions o;
  Words rest;
  std::string error;

  /* the client, as the fork's tests start it */
  FACT_CHECK( ( take( { "-L", "8080:h:80", "-R", "9090:g:90", "127.0.0.1", "60001" }, "#", true, o, rest, error )
           && o.tcp == Words{ "8080:h:80" } && o.tcp_remote == Words{ "9090:g:90" } && o.udp.empty()
           && rest == Words{ "127.0.0.1", "60001" } ), "REQ1/FWD-1: -L and -R with their values in the next word" );
  FACT_CHECK( ( take( { "-L8080:h:80", "ip", "port" }, "#", true, o, rest, error ) && o.tcp == Words{ "8080:h:80" }
           && rest == Words{ "ip", "port" } ), "REQ1/FWD-1: a value joined to its letter" );
  FACT_CHECK( ( take( { "-LU", "5353:ns:53", "-RU", "53:ns:5353", "ip", "port" }, "#", true, o, rest, error )
           && o.udp == Words{ "5353:ns:53" } && o.udp_remote == Words{ "53:ns:5353" } && o.tcp.empty()
           && rest == Words{ "ip", "port" } ), "REQ1/FWD-2: -LU and -RU take the specification from the word after" );
  FACT_CHECK( ( take( { "-L", "1:a:1", "-L", "2:b:2", "ip", "port" }, "#", true, o, rest, error )
           && o.tcp == Words{ "1:a:1", "2:b:2" } ), "several of a kind are kept in order" );
  FACT_CHECK( ( take( { "-N", "-L", "1:a:1", "ip", "port" }, "#", true, o, rest, error ) && o.headless
           && rest == Words{ "ip", "port" } ), "REQ1/FWD-5: -N is the client's, and is taken" );
  FACT_CHECK( ( take( { "-vN", "ip", "port" }, "#", true, o, rest, error ) && o.headless && rest == Words{ "-v", "ip", "port" } ), "-N sharing a word with one of the core's letters" );
  FACT_CHECK( ( take( { "-F", "always", "ip", "port" }, "#", true, o, rest, error ) && o.parity == "always"
           && take( { "ip", "port" }, "#", true, o, rest, error ) && o.parity == "auto" ), "-F sets the parity word, which defaults to auto" );
  FACT_CHECK( ( take( { "-X", "@sh\x1ftr a-z A-Z", "ip", "port" }, "#", true, o, rest, error ) && o.stream == "@sh\x1ftr a-z A-Z"
           && o.headless && !o.any() && rest == Words{ "ip", "port" } ), "REQ1/CP-7: -X takes a stream's words whole, and at the client means no terminal" );
  FACT_CHECK( ( take( { "-X", "@stdio", "--", "sh" }, "@ipcl", false, o, rest, error ) && o.stream == "@stdio" && !o.headless
           && rest == Words{ "--", "sh" } ), "at the server -X is taken too, and does not mean headless, which is not the server's" );
  FACT_CHECK( ( take( { "-X", "", "ip", "port" }, "#", true, o, rest, error ) && o.stream_given && o.stream.empty() && o.headless
           && take( { "ip", "port" }, "#", true, o, rest, error ) && !o.stream_given ), "an empty -X is still a -X: it is for the caller to refuse, not to be taken for none" );
  FACT_CHECK( ( take( { "-#", "ARGS -L not ours", "-v", "ip", "port" }, "#", true, o, rest, error ) && !o.any() && !o.headless
           && rest == Words{ "-#", "ARGS -L not ours", "-v", "ip", "port" } ), "with nothing of gosh's, the words are untouched and nothing is asked for" );
  FACT_CHECK( ( take( { "-#", "-L", "ip", "port" }, "#", true, o, rest, error ) && !o.any()
           && rest == Words{ "-#", "-L", "ip", "port" } ), "a value of one of the core's letters is never read as an option" );

  FACT_CHECK( ( take( { "127.0.0.1", "60001", "-N", "-L", "1:a:1" }, "#", true, o, rest, error ) && o.headless
           && o.tcp == Words{ "1:a:1" } && rest == Words{ "127.0.0.1", "60001" } ), "the client's words may follow the address, as the fork's getopt allowed" );

  /* the server: what follows "new" */
  FACT_CHECK( ( take( { "-i", "127.0.0.1", "-L", "1:a:1", "-p", "60001", "-c", "256", "--", "sh", "-c", "x -L y" }, "@ipcl", false,
               o, rest, error )
           && o.tcp == Words{ "1:a:1" }
           && rest == Words{ "-i", "127.0.0.1", "-p", "60001", "-c", "256", "--", "sh", "-c", "x -L y" } ), "the server's own options with values pass through, around gosh's" );
  FACT_CHECK( ( take( { "--", "-L", "1:a:1" }, "@ipcl", false, o, rest, error ) && !o.any()
           && rest == Words{ "--", "-L", "1:a:1" } ), "after \"--\" nothing is gosh's" );
  FACT_CHECK( ( take( { "-N" }, "@ipcl", false, o, rest, error ) && !o.headless && rest == Words{ "-N" } ), "-N is not the server's: it is left for the core to refuse" );

  /* what cannot be taken */
  FACT_CHECK( ( !take( { "-L" }, "#", true, o, rest, error ) && error == "-L needs a value" ), "-L with nothing after it is an error that names it" );
  FACT_CHECK( ( !take( { "-RU" }, "#", true, o, rest, error ) && error == "-RU needs a forwarding specification" ), "-RU with no specification is an error that names it" );

  /* REQ6/NAME-1: which program it has been asked to be */
  std::string called;
  size_t skip = 9;
  FACT_CHECK( ( Gosh::which_role( "/home/u/.local/bin/gosh", { "host" }, called, skip ) == Gosh::THE_COMMAND && called == "gosh"
           && skip == 0 ), "REQ6/NAME-1: under the name gosh it is the command" );
  FACT_CHECK( ( Gosh::which_role( "gocp", { "a", "host:b" }, called, skip ) == Gosh::THE_COPY && called == "gocp" && skip == 0 ), "REQ6/NAME-1: under the name gocp it is the copy command" );
  FACT_CHECK( ( Gosh::which_role( "./gosh-client", { "gosh", "host" }, called, skip ) == Gosh::THE_COMMAND && called == "gosh"
           && skip == 1 ), "with gosh as its first word it is the command, and the word is passed over" );
  FACT_CHECK( ( Gosh::which_role( "C:\\bin\\gosh.exe", {}, called, skip ) == Gosh::THE_COMMAND && called == "gosh" ), "a Windows path and .exe are not part of the name" );
  FACT_CHECK( ( Gosh::which_role( "/usr/bin/gosh-client", { "127.0.0.1", "60001" }, called, skip ) == Gosh::THE_CLIENT
           && called == "gosh-client" && skip == 0 ), "under its own name, with an address first, it is the client" );
  typedef std::vector<std::string> Words;
  FACT_CHECK( Gosh::operands( { "-v", "192.0.2.1", "60001" }, "#" ) == Words( { "192.0.2.1", "60001" } ), "the server's address and port are found last" );
  FACT_CHECK( Gosh::operands( { "192.0.2.1", "60001", "-v" }, "#" ) == Words( { "192.0.2.1", "60001" } ), "and first, with an option after them" );
  FACT_CHECK( ( Gosh::operands( { "192.0.2.1", "-#", "5", "6" }, "#" ) == Words( { "192.0.2.1", "6" } )
           && Gosh::operands( { "-#5", "192.0.2.1", "6" }, "#" ) == Words( { "192.0.2.1", "6" } )
           && Gosh::operands( { "-v#", "5", "192.0.2.1", "6" }, "#" ) == Words( { "192.0.2.1", "6" } ) ), "an option's value is not one of them, as the next word or joined on" );
  FACT_CHECK( ( Gosh::operands( { "-v", "--", "-x", "6" }, "#" ) == Words( { "-x", "6" } ) && Gosh::operands( { "-" }, "#" ) == Words( { "-" } ) ), "after \"--\" every word is one, and a lone \"-\" is one" );
  FACT_CHECK( ( Gosh::which_role( "mosh", { "host" }, called, skip ) == Gosh::THE_CLIENT
           && Gosh::which_role( "gosh-client", { "mosh", "host" }, called, skip ) == Gosh::THE_CLIENT && skip == 0 ), "the names it used to have are not the command (no alias but REQ17/CMD-4's)" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Options );
}

FACTS_MAIN
