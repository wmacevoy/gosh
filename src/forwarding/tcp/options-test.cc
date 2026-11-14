/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/* REQ1/FWD-1: a segment's options, read (options.h). */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/tcp/options.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-90s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static std::string bytes( const std::vector<int>& values )
{
  std::string s;
  for ( size_t i = 0; i < values.size(); i++ ) {
    s.push_back( static_cast<char>( values[i] ) );
  }
  return s;
}

int main()
{
  using Gosh::SegmentOptions;
  /* an MSS of 1,174 (kind 2), padding, a window shift of 5 (kind 3), padding twice, selective acknowledgment permitted (kind 4) */
  const std::string all = bytes( { 2, 4, 0x04, 0x96, 1, 3, 3, 5, 1, 1, 4, 2 } );
  check( "the largest segment asked for is read: 1,174", SegmentOptions::mss( all, 536 ) == 1174 );
  check( "the window shift is read: 5", SegmentOptions::scale( all ) == 5 );
  check( "selective acknowledgment permitted is read", SegmentOptions::sack_permitted( all ) );

  check( "no options: the size is what the caller says, no shift was asked, nothing permitted",
         SegmentOptions::mss( "", 536 ) == 536 && SegmentOptions::scale( "" ) == -1 && !SegmentOptions::sack_permitted( "" ) );
  check( "a shift of more than 14 is read as 14", SegmentOptions::scale( bytes( { 3, 3, 200 } ) ) == 14 );
  check( "after the end of the list (kind 0) nothing is read", !SegmentOptions::sack_permitted( bytes( { 0, 2, 4, 2 } ) ) );
  check( "an option cut short is not read", SegmentOptions::mss( bytes( { 2, 4, 0x04 } ), 536 ) == 536 );
  check( "a length that cannot be (less than 2) ends the reading", !SegmentOptions::sack_permitted( bytes( { 9, 1, 4, 2 } ) ) );
  check( "an unknown kind is passed over by its length", SegmentOptions::sack_permitted( bytes( { 9, 3, 77, 4, 2 } ) ) );
  check( "the right kind with the wrong length is not that option",
         SegmentOptions::mss( bytes( { 2, 3, 9 } ), 536 ) == 536 && !SegmentOptions::sack_permitted( bytes( { 4, 3, 0 } ) ) );

  std::string four;
  SegmentOptions::put32( four, 0x01020304 );
  SegmentOptions::put32( four, 0xFFFFFFFE );
  check( "a 32-bit number is written most significant byte first, and read back",
         four == bytes( { 1, 2, 3, 4, 255, 255, 255, 254 } ) && SegmentOptions::get32( four, 0 ) == 0x01020304
           && SegmentOptions::get32( four, 4 ) == 0xFFFFFFFE );

  if ( failures ) {
    printf( "FAIL REQ1/FWD-1: %d checks of a segment's options failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/FWD-1: a segment's options\n" );
  return EXIT_SUCCESS;
}
