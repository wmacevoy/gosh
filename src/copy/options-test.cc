/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-1, REQ1/CP-5: the words of one end of a copy (options.h): which end, its flags, its
 * paths; and every way they can be wrong, refused.
 */

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "src/copy/options.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static bool parsed( const std::string& line, FileCopy::Options& o )
{
  std::vector<std::string> words;
  std::istringstream in( line );
  std::string w;
  while ( in >> w ) {
    words.push_back( w );
  }
  o = FileCopy::Options();
  return FileCopy::parse( words, o );
}

int main()
{
  FileCopy::Options o;
  check( "a sender of two sources (REQ1/CP-1)",
         parsed( "send -- a b", o ) && o.sender && o.paths.size() == 2 && o.paths[1] == "b" && !o.recursive );
  check( "a receiver of one destination", parsed( "receive -- d", o ) && !o.sender && o.paths.size() == 1 );
  check( "each flag (REQ1/CP-5: --auto, --delete, --interval)",
         parsed( "send -r --auto --delete --glob -q --interval=250 -- a", o ) && o.recursive && o.automatic
           && o.remove && o.glob && o.quiet && o.interval == 250 );
  check( "unset, they are off, and the interval is 2 s",
         parsed( "send -- a", o ) && !o.automatic && !o.remove && !o.glob && !o.quiet && o.interval == 2000 );
  check( "a path that looks like a flag, after \"--\", is a path",
         parsed( "send -- -r", o ) && o.paths.size() == 1 && o.paths[0] == "-r" && !o.recursive );

  check( "refused: neither send nor receive", !parsed( "copy -- a", o ) && !parsed( "", o ) );
  check( "refused: a word it does not know", !parsed( "send -x -- a", o ) );
  check( "refused: no \"--\"", !parsed( "send a", o ) && !parsed( "send -r", o ) && !parsed( "receive", o ) );
  check( "refused: a sender with nothing to send", !parsed( "send --", o ) );
  check( "refused: a receiver with none, or two, destinations", !parsed( "receive --", o ) && !parsed( "receive -- a b", o ) );
  check( "refused: an interval under 100 ms, or not a number",
         !parsed( "send --interval=99 -- a", o ) && !parsed( "send --interval=x -- a", o ) );
  check( "100 ms is allowed", parsed( "send --interval=100 -- a", o ) && o.interval == 100 );

  if ( failures ) {
    printf( "FAIL REQ1/CP-1, REQ1/CP-5: %d checks of the options failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/CP-1, REQ1/CP-5: the options\n" );
  return 0;
}
