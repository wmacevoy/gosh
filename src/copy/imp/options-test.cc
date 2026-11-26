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
#include "vendor/facts/facts.h"



static bool parsed( const std::string& line, Gosh::Copy::Options& o )
{
  std::vector<std::string> words;
  std::istringstream in( line );
  std::string w;
  while ( in >> w ) {
    words.push_back( w );
  }
  o = Gosh::Copy::Options();
  return Gosh::Copy::parse( words, o );
}
/* REQ1/CP-1, REQ1/CP-5: the options */
FACTS( Options )
{
  Gosh::Copy::Options o;
  FACT_CHECK( ( parsed( "send -- a b", o ) && o.sender && o.paths.size() == 2 && o.paths[1] == "b" && !o.recursive ), "a sender of two sources (REQ1/CP-1)" );
  FACT_CHECK( ( parsed( "receive -- d", o ) && !o.sender && o.paths.size() == 1 ), "a receiver of one destination" );
  FACT_CHECK( ( parsed( "send -r --auto --delete --glob -q --interval=250 -- a", o ) && o.recursive && o.automatic
           && o.remove && o.glob && o.quiet && o.interval == 250 ), "each flag (REQ1/CP-5: --auto, --delete, --interval)" );
  FACT_CHECK( ( parsed( "send -- a", o ) && !o.automatic && !o.remove && !o.glob && !o.quiet && o.interval == 2000 ), "unset, they are off, and the interval is 2 s" );
  FACT_CHECK( ( parsed( "send -- -r", o ) && o.paths.size() == 1 && o.paths[0] == "-r" && !o.recursive ), "a path that looks like a flag, after \"--\", is a path" );

  FACT_CHECK( ( !parsed( "copy -- a", o ) && !parsed( "", o ) ), "refused: neither send nor receive" );
  FACT_CHECK( !parsed( "send -x -- a", o ), "refused: a word it does not know" );
  FACT_CHECK( ( !parsed( "send a", o ) && !parsed( "send -r", o ) && !parsed( "receive", o ) ), "refused: no \"--\"" );
  FACT_CHECK( !parsed( "send --", o ), "refused: a sender with nothing to send" );
  FACT_CHECK( ( !parsed( "receive --", o ) && !parsed( "receive -- a b", o ) ), "refused: a receiver with none, or two, destinations" );
  FACT_CHECK( ( !parsed( "send --interval=99 -- a", o ) && !parsed( "send --interval=x -- a", o ) ), "refused: an interval under 100 ms, or not a number" );
  FACT_CHECK( ( parsed( "send --interval=100 -- a", o ) && o.interval == 100 ), "100 ms is allowed" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Options );
}

FACTS_MAIN
