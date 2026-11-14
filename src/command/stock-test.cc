/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/* REQ8/CMD-3: what gosh says when the server it got is a stock mosh's, and the line that picks the server. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/command/stock.h"

static int failures = 0;

static void check( const char* what, bool ok, const std::string& got = "" )
{
  printf( "%-70s %s%s\n", what, ok ? "ok" : "FAIL ", ok ? "" : got.c_str() );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  using Words = std::vector<std::string>;

  std::string said = Gosh::stock_notice( "gosh", "u@host", Words(), false );
  check( "with nothing of gosh's asked for, it says only which server it got",
         said == "gosh: u@host has no gosh-server, so its mosh-server is used.", said );

  said = Gosh::stock_notice( "gosh", "host", { "-L", "8080:h:80", "-F", "always", "-RU", "53:ns:5353" }, false );
  check( "each forward asked for is named as not done; -F is not one",
         said == "gosh: host has no gosh-server, so its mosh-server is used."
                 " That one cannot do these, and is not asked to: -L 8080:h:80; -RU 53:ns:5353.",
         said );

  said = Gosh::stock_notice( "gosh", "host", { "-R", "1:a:1" }, true );
  check( "the ssh fallback, when asked for, is named too",
         said == "gosh: host has no gosh-server, so its mosh-server is used."
                 " That one cannot do these, and is not asked to: -R 1:a:1; the ssh fallback (--relay).",
         said );

  const std::string line = Gosh::server_command( "'new' '-L' '1:a:1'", "'new'" );
  check( "the host's line is one word for sh -c", line.compare( 0, 7, "sh -c '" ) == 0 && line[line.size() - 1] == '\'', line );
  check( "it tries gosh-server first, with all the words",
         line.find( "exec gosh-server '\\''new'\\'' '\\''-L'\\'' '\\''1:a:1'\\''; fi;" ) != std::string::npos, line );
  check( "then says which it is, and starts mosh-server with only its own words",
         line.find( "\"GOSH SERVER mosh-server\"; exec mosh-server '\\''new'\\'''" ) != std::string::npos, line );
  check( "the stock line is what the host is told to print", std::string( Gosh::STOCK_SERVER_LINE ) == "GOSH SERVER mosh-server" );

  if ( failures ) {
    printf( "FAIL REQ8/CMD-3: %d checks failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ8/CMD-3: what is said of a stock server, and the line that picks the server\n" );
  return EXIT_SUCCESS;
}
