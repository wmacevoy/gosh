/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/* REQ14 (selective acknowledgment): what the other end says it holds (held.h), with numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/tcp/held.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-90s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

/* An acknowledgment's option naming these ranges (kind 5), as sequence numbers. */
static std::string says( const std::vector<std::pair<uint32_t, uint32_t> >& ranges )
{
  std::string option;
  option.push_back( 1 );
  option.push_back( 1 );
  option.push_back( 5 );
  option.push_back( static_cast<char>( 2 + 8 * ranges.size() ) );
  for ( size_t i = 0; i < ranges.size(); i++ ) {
    Gosh::SegmentOptions::put32( option, ranges[i].first );
    Gosh::SegmentOptions::put32( option, ranges[i].second );
  }
  return option;
}

static void test_at( uint32_t una, const char* where )
{
  printf( "%s\n", where );
  Gosh::Held h;
  uint32_t at = 0, end = 0;
  check( "  nothing held: no gap to fill, nothing to pass over", h.empty() && !h.next_gap( una, una, at, end ) && h.below( una, 5000 ) == 0 );

  /* 10,000 bytes sent. It says it holds 2,000 to 3,000 and 5,000 to 6,000. */
  h.note( says( { { una + 5000, una + 6000 }, { una + 2000, una + 3000 } } ), una, 10000 );
  check( "  two ranges told of, in either order: 2,000 held below 10,000; 500 below 2,500; none below 2,000",
         h.below( una, 10000 ) == 2000 && h.below( una, 2500 ) == 500 && h.below( una, 2000 ) == 0 );
  check( "  the first gap is 0 to 2,000", h.next_gap( una, una, at, end ) && at == 0 && end == 2000 );
  check( "  from 2,000 on, the next gap is 3,000 to 5,000", h.next_gap( una, una + 2000, at, end ) && at == 3000 && end == 5000 );
  check( "  from 4,000 on, it is 4,000 to 5,000", h.next_gap( una, una + 4000, at, end ) && at == 4000 && end == 5000 );
  check( "  beyond the last range held there is no gap: it may not have arrived yet", !h.next_gap( una, una + 5500, at, end ) );

  uint32_t nxt = una + 2500;
  check( "  sending again from inside a range: passed over to its end, with 2,000 to go before the next",
         h.pass_over( una, nxt ) == 2000 && nxt == una + 3000 );
  nxt = una + 500;
  check( "  from before a range: not moved, with 1,500 to go before it", h.pass_over( una, nxt ) == 1500 && nxt == una + 500 );
  nxt = una + 7000;
  check( "  from beyond them all: not moved, and nothing ahead", h.pass_over( una, nxt ) == 0xFFFFFFFF && nxt == una + 7000 );

  /* ranges that touch or overlap become one */
  h.note( says( { { una + 3000, una + 5000 } } ), una, 10000 );
  check( "  told of what lies between two ranges: they are one, 2,000 to 6,000",
         h.below( una, 10000 ) == 4000 && h.next_gap( una, una + 2000, at, end ) == false );
  h.note( says( { { una + 5500, una + 7000 } } ), una, 10000 );
  check( "  told of a range that overlaps: it grows to 7,000, counted once", h.below( una, 10000 ) == 5000 );

  /* what is not believed */
  Gosh::Held k;
  k.note( says( { { una, una + 1000 } } ), una, 10000 );
  check( "  a range that begins at what is not yet acknowledged is not believed", k.empty() );
  k.note( says( { { una + 9000, una + 10001 } } ), una, 10000 );
  check( "  nor one that ends past what was sent", k.empty() );
  k.note( says( { { una + 3000, una + 3000 } } ), una, 10000 );
  k.note( says( { { una + 4000, una + 3000 } } ), una, 10000 );
  k.note( says( { { una - 500, una + 300 } } ), una, 10000 );
  check( "  nor an empty one, one that runs backwards, or one from before", k.empty() );
  std::string cut = says( { { una + 1000, una + 2000 } } );
  k.note( cut.substr( 0, cut.size() - 1 ), una, 10000 );
  check( "  nor an option cut short", k.empty() );
  k.note( says( { { una + 9000, una + 10000 } } ), una, 10000 );
  check( "  one that ends exactly where what was sent ends is", !k.empty() && k.below( una, 10000 ) == 1000 );

  /* acknowledgment moves on */
  h.forget( una + 1000 );
  check( "  acknowledged to 1,000: the range (now 1,000 to 6,000 from there) is kept",
         h.below( una + 1000, 9000 ) == 5000 && h.next_gap( una + 1000, una + 1000, at, end ) && at == 0 && end == 1000 );
  h.forget( una + 4000 );
  check( "  acknowledged to 4,000, inside the range: it was never held, and is forgotten", h.empty() );
}

int main()
{
  test_at( 1000000, "sequence numbers in the middle of their range:" );
  test_at( 0xFFFFF000u, "sequence numbers that wrap round within the data:" );

  if ( failures ) {
    printf( "FAIL REQ14 (selective acknowledgment): %d checks of what the other end holds failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ14 (selective acknowledgment): what the other end holds\n" );
  return EXIT_SUCCESS;
}
