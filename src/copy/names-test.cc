/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-6, REQ1/CP-3: names (names.h). A name from the other end that could land outside the
 * destination is refused, every way one might be written; a safe one lands where it should; a file
 * arrives under a part name beside its own (".NAME.mocp-part", pf.7's); and a source goes by its
 * own last name.
 */

#include <cstdio>
#include <string>

#include "src/copy/names.h"
#include "vendor/facts/facts.h"
/* REQ1/CP-6, REQ1/CP-3: names */
FACTS( Names )
{
  using namespace FileCopy;

  FACT_CHECK( ( safe_relative_path( "a" ) && safe_relative_path( "a/b/c" ) && safe_relative_path( "./a" )
           && safe_relative_path( "." ) && safe_relative_path( "..a" ) && safe_relative_path( "a.." ) ), "safe: a name, a path, and \"./\" leading (REQ1/CP-6)" );
  FACT_CHECK( !safe_relative_path( "" ), "not safe: empty" );
  FACT_CHECK( ( !safe_relative_path( "/a" ) && !safe_relative_path( "/" ) ), "not safe: absolute" );
  FACT_CHECK( ( !safe_relative_path( ".." ) && !safe_relative_path( "../a" ) && !safe_relative_path( "a/../b" )
           && !safe_relative_path( "a/.." ) ), "not safe: \"..\" anywhere" );
  FACT_CHECK( ( !safe_relative_path( "a//b" ) && !safe_relative_path( "a/" ) ), "not safe: an empty part (\"a//b\", \"a/\")" );
  FACT_CHECK( ( !safe_relative_path( "a/./b" ) && !safe_relative_path( "./." ) ), "not safe: \".\" but first (\"a/./b\")" );
  FACT_CHECK( !safe_relative_path( std::string( "a\0b", 3 ) ), "not safe: a NUL in it" );

  FACT_CHECK( target_of( "/d", true, "x/y" ) == "/d/x/y", "into a directory: under it" );
  FACT_CHECK( target_of( "/d", false, "./x" ) == "/d/x", "\"./\" leading: inside the destination itself" );
  FACT_CHECK( target_of( "/d", true, "." ) == "/d", "\".\": the destination itself" );
  FACT_CHECK( ( target_of( "/d/new", false, "src" ) == "/d/new" && target_of( "/d/new", false, "src/a/b" ) == "/d/new/a/b" ), "the one thing copied takes the destination's name, with what is under it" );
  FACT_CHECK( target_of( "/", true, "x" ) == "/x", "a destination of \"/\": no doubled slash" );

  FACT_CHECK( ( part_name( "/d/sub/b.bin" ) == "/d/sub/.b.bin.mocp-part" && part_name( "b.bin" ) == ".b.bin.mocp-part" ), "the part file: \".NAME.mocp-part\" beside it (REQ1/CP-3)" );

  FACT_CHECK( ( child_of( "/", "sub" ) == "/sub" && child_of( "/", "sub" ) == target_of( "/", true, "sub" )
           && child_of( "/d", "x" ) == "/d/x" && child_of( "/d", "x" ) == target_of( "/d", true, "x" ) ), "a child of \"/\" is the same text target_of gives (REQ1/CP-5, --delete)" );
  FACT_CHECK( ( is_part_name( ".x.mocp-part" ) && is_part_name( "d/.x.mocp-part" ) && is_part_name( part_name( "/d/b.bin" ) ) ), "a part file's name is known for one (REQ1/CP-3)" );
  FACT_CHECK( ( !is_part_name( "x.mocp-part" ) && !is_part_name( "..mocp-part" ) && !is_part_name( ".mocp-part" )
           && !is_part_name( ".x.mocp-part/y" ) && !is_part_name( "x" ) && !is_part_name( ".x.mocp-partial" ) ), "and nothing else is" );

  FACT_CHECK( ( top_name( "/x/y/file" ) == "file" && top_name( "dir/" ) == "dir" && top_name( "file" ) == "file" ), "a source goes by its last name" );
  FACT_CHECK( ( top_name( "src/." ) == "." && top_name( ".." ) == "." && top_name( "/" ) == "." ), "\".\", \"..\" and \"/\": what is in them, not them" );

  FACT_CHECK( ( expand_home( "~", "/h" ) == "/h" && expand_home( "~/a", "/h" ) == "/h/a" ), "\"~\" and \"~/\" under the home given" );
  FACT_CHECK( ( expand_home( "~a", "/h" ) == "~a" && expand_home( "a/~", "/h" ) == "a/~" && expand_home( "~/a", NULL ) == "~/a" ), "anything else, and no home: as it is" );
  FACT_CHECK( ( without_trailing_slashes( "a//" ) == "a" && without_trailing_slashes( "/" ) == "/" ), "trailing slashes go, but \"/\" stays" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Names );
}

FACTS_MAIN
