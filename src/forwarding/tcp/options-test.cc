/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/FWD-1: a segment's options, read (options.h). */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/tcp/options.h"
#include "vendor/facts/facts.h"



static std::string bytes( const std::vector<int>& values )
{
  std::string s;
  for ( size_t i = 0; i < values.size(); i++ ) {
    s.push_back( static_cast<char>( values[i] ) );
  }
  return s;
}
/* REQ1/FWD-1: a segment's options */
FACTS( Options )
{
  using Gosh::SegmentOptions;
  /* an MSS of 1,174 (kind 2), padding, a window shift of 5 (kind 3), padding twice, selective acknowledgment permitted (kind 4) */
  const std::string all = bytes( { 2, 4, 0x04, 0x96, 1, 3, 3, 5, 1, 1, 4, 2 } );
  FACT_CHECK( SegmentOptions::mss( all, 536 ) == 1174, "the largest segment asked for is read: 1,174" );
  FACT_CHECK( SegmentOptions::scale( all ) == 5, "the window shift is read: 5" );
  FACT_CHECK( SegmentOptions::sack_permitted( all ), "selective acknowledgment permitted is read" );

  FACT_CHECK( ( SegmentOptions::mss( "", 536 ) == 536 && SegmentOptions::scale( "" ) == -1 && !SegmentOptions::sack_permitted( "" ) ), "no options: the size is what the caller says, no shift was asked, nothing permitted" );
  FACT_CHECK( SegmentOptions::scale( bytes( { 3, 3, 200 } ) ) == 14, "a shift of more than 14 is read as 14" );
  FACT_CHECK( !SegmentOptions::sack_permitted( bytes( { 0, 2, 4, 2 } ) ), "after the end of the list (kind 0) nothing is read" );
  FACT_CHECK( SegmentOptions::mss( bytes( { 2, 4, 0x04 } ), 536 ) == 536, "an option cut short is not read" );
  FACT_CHECK( !SegmentOptions::sack_permitted( bytes( { 9, 1, 4, 2 } ) ), "a length that cannot be (less than 2) ends the reading" );
  FACT_CHECK( SegmentOptions::sack_permitted( bytes( { 9, 3, 77, 4, 2 } ) ), "an unknown kind is passed over by its length" );
  FACT_CHECK( ( SegmentOptions::mss( bytes( { 2, 3, 9 } ), 536 ) == 536 && !SegmentOptions::sack_permitted( bytes( { 4, 3, 0 } ) ) ), "the right kind with the wrong length is not that option" );

  std::string four;
  SegmentOptions::put32( four, 0x01020304 );
  SegmentOptions::put32( four, 0xFFFFFFFE );
  FACT_CHECK( ( four == bytes( { 1, 2, 3, 4, 255, 255, 255, 254 } ) && SegmentOptions::get32( four, 0 ) == 0x01020304
           && SegmentOptions::get32( four, 4 ) == 0xFFFFFFFE ), "a 32-bit number is written most significant byte first, and read back" );

  /* (from the review of 2026-11-15: the size was only ever first in the list, and 536 the only fallback) */
  FACT_CHECK( SegmentOptions::mss( bytes( { 1, 3, 3, 5, 1, 2, 4, 0x05, 0x00 } ), 536 ) == 1280, "the largest segment asked for is read from later in the list, after padding and another option" );
  FACT_CHECK( SegmentOptions::mss( bytes( { 3, 3, 5 } ), 1000 ) == 1000, "it is whatever the caller says when it is not there: 1,000, asked so" );
  FACT_CHECK( ( SegmentOptions::mss( bytes( { 0, 2, 2, 4, 0x05, 0x00 } ), 536 ) == 536 && SegmentOptions::scale( bytes( { 0, 2, 3, 3, 5 } ) ) == -1 ), "after the end of the list neither the size nor the shift is read" );
  FACT_CHECK( ( SegmentOptions::mss( bytes( { 9, 1, 2, 4, 0x05, 0x00 } ), 536 ) == 536 && SegmentOptions::scale( bytes( { 9, 1, 3, 3, 5 } ) ) == -1 ), "a length that cannot be ends the reading of each" );
  FACT_CHECK( ( SegmentOptions::scale( bytes( { 3, 4, 5, 0 } ) ) == -1 && SegmentOptions::scale( bytes( { 3, 3 } ) ) == -1 ), "the shift with the wrong length, or cut short, is not a shift" );
  FACT_CHECK( ( SegmentOptions::mss( bytes( { 2, 4, 0x04, 0x00, 2, 4, 0x05, 0x00 } ), 536 ) == 1024
           && SegmentOptions::scale( bytes( { 3, 3, 4, 3, 3, 9 } ) ) == 4 ), "asked twice, the first is what was asked: the size, and the shift" );
  FACT_CHECK( SegmentOptions::scale( bytes( { 9, 3, 77, 3, 3, 7 } ) ) == 7, "the shift is read after an unknown option" );
  FACT_CHECK( ( SegmentOptions::get32( bytes( { 0, 0, 0, 0xFE } ), 0 ) == 0x000000FE && SegmentOptions::get32( bytes( { 0, 0x80, 0x90, 0xA0 } ), 0 ) == 0x008090A0 ), "a number whose last byte is 0xFE is read as itself, not as a negative one" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Options );
}

FACTS_MAIN
