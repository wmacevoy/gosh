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

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  using namespace FileCopy;

  check( "safe: a name, a path, and \"./\" leading (REQ1/CP-6)",
         safe_relative_path( "a" ) && safe_relative_path( "a/b/c" ) && safe_relative_path( "./a" )
           && safe_relative_path( "." ) && safe_relative_path( "..a" ) && safe_relative_path( "a.." ) );
  check( "not safe: empty", !safe_relative_path( "" ) );
  check( "not safe: absolute", !safe_relative_path( "/a" ) && !safe_relative_path( "/" ) );
  check( "not safe: \"..\" anywhere",
         !safe_relative_path( ".." ) && !safe_relative_path( "../a" ) && !safe_relative_path( "a/../b" )
           && !safe_relative_path( "a/.." ) );
  check( "not safe: an empty part (\"a//b\", \"a/\")", !safe_relative_path( "a//b" ) && !safe_relative_path( "a/" ) );
  check( "not safe: \".\" but first (\"a/./b\")", !safe_relative_path( "a/./b" ) && !safe_relative_path( "./." ) );
  check( "not safe: a NUL in it", !safe_relative_path( std::string( "a\0b", 3 ) ) );

  check( "into a directory: under it", target_of( "/d", true, "x/y" ) == "/d/x/y" );
  check( "\"./\" leading: inside the destination itself", target_of( "/d", false, "./x" ) == "/d/x" );
  check( "\".\": the destination itself", target_of( "/d", true, "." ) == "/d" );
  check( "the one thing copied takes the destination's name, with what is under it",
         target_of( "/d/new", false, "src" ) == "/d/new" && target_of( "/d/new", false, "src/a/b" ) == "/d/new/a/b" );
  check( "a destination of \"/\": no doubled slash", target_of( "/", true, "x" ) == "/x" );

  check( "the part file: \".NAME.mocp-part\" beside it (REQ1/CP-3)",
         part_name( "/d/sub/b.bin" ) == "/d/sub/.b.bin.mocp-part" && part_name( "b.bin" ) == ".b.bin.mocp-part" );

  check( "a source goes by its last name",
         top_name( "/x/y/file" ) == "file" && top_name( "dir/" ) == "dir" && top_name( "file" ) == "file" );
  check( "\".\", \"..\" and \"/\": what is in them, not them",
         top_name( "src/." ) == "." && top_name( ".." ) == "." && top_name( "/" ) == "." );

  check( "\"~\" and \"~/\" under the home given", expand_home( "~", "/h" ) == "/h" && expand_home( "~/a", "/h" ) == "/h/a" );
  check( "anything else, and no home: as it is",
         expand_home( "~a", "/h" ) == "~a" && expand_home( "a/~", "/h" ) == "a/~" && expand_home( "~/a", NULL ) == "~/a" );
  check( "trailing slashes go, but \"/\" stays",
         without_trailing_slashes( "a//" ) == "a" && without_trailing_slashes( "/" ) == "/" );

  if ( failures ) {
    printf( "FAIL REQ1/CP-6, REQ1/CP-3: %d checks of names failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/CP-6, REQ1/CP-3: names\n" );
  return 0;
}
