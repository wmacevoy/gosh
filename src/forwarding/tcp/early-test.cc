/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ14 (selective acknowledgment), REQ1/FWD-1: segments that came early (early.h), with numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/tcp/early.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-90s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static const size_t LIMIT = 1024 * 1024;

/* The ranges an option names (kind 5), less `base`. */
static std::vector<std::pair<uint32_t, uint32_t> > ranges_of( const std::string& option, uint32_t base )
{
  std::vector<std::pair<uint32_t, uint32_t> > found;
  for ( size_t at = 4; at + 8 <= option.size(); at += 8 ) {
    found.push_back( std::make_pair( Gosh::SegmentOptions::get32( option, at ) - base,
                                     Gosh::SegmentOptions::get32( option, at + 4 ) - base ) );
  }
  return found;
}

typedef std::vector<std::pair<uint32_t, uint32_t> > Ranges;

static void test_at( uint32_t nxt, const char* where )
{
  printf( "%s\n", where );
  Gosh::Early e;
  check( "  nothing kept: nothing to say, nothing to take", e.empty() && e.blocks( nxt, LIMIT, 4 ).empty() );

  const std::string hundred( 100, 'x' );
  e.keep( nxt + 300, hundred, nxt, LIMIT );
  e.keep( nxt + 400, hundred, nxt, LIMIT ); /* touches the first */
  e.keep( nxt + 1000, hundred, nxt, LIMIT );
  e.keep( nxt + 2000, hundred, nxt, LIMIT );
  e.keep( nxt + 3000, hundred, nxt, LIMIT );
  e.keep( nxt + 4000, hundred, nxt, LIMIT );
  e.keep( nxt + 1100, hundred, nxt, LIMIT ); /* the last to arrive: it joins 1,000 to 1,100 */
  const std::string said = e.blocks( nxt, LIMIT, 4 );
  check( "  five ranges held: the option is 2 bytes of padding, kind 5, length 34: four ranges",
         said.size() == 36 && said[0] == 1 && said[1] == 1 && said[2] == 5 && said[3] == 34 );
  check( "  the range of the last arrival first (1,000 to 1,200), then the nearest: 300-500, 2,000-2,100, 3,000-3,100",
         ranges_of( said, nxt ) == Ranges( { { 1000, 1200 }, { 300, 500 }, { 2000, 2100 }, { 3000, 3100 } } ) );
  check( "  room for two only: the last arrival's and the nearest",
         ranges_of( e.blocks( nxt, LIMIT, 2 ), nxt ) == Ranges( { { 1000, 1200 }, { 300, 500 } } ) );
  check( "  room for none: nothing is said", e.blocks( nxt, LIMIT, 0 ).empty() );

  e.keep( nxt + 300, std::string( 50, 'y' ), nxt, LIMIT );
  check( "  one that begins where one kept begins is not kept twice", ranges_of( e.blocks( nxt, LIMIT, 1 ), nxt ) == Ranges( { { 1000, 1200 } } ) );
  e.keep( nxt + 5000, "", nxt, LIMIT );
  e.keep( nxt + LIMIT + 1, hundred, nxt, LIMIT );
  check( "  nor an empty one, nor one further ahead than the buffer is long",
         ranges_of( e.blocks( nxt, LIMIT, 4 ), nxt ).size() == 4 && ranges_of( e.blocks( nxt, LIMIT, 4 ), nxt )[0].first == 1000 );

  uint32_t seq = 0;
  std::string payload;
  check( "  the next byte wanted is not held: nothing to take", !e.take_next( nxt, LIMIT, seq, payload ) );
  check( "  in order up to 300: the segment at 300 is taken", e.take_next( nxt + 300, LIMIT, seq, payload ) && seq == nxt + 300 && payload == hundred );
  check( "  then the one at 400", e.take_next( nxt + 400, LIMIT, seq, payload ) && seq == nxt + 400 );
  check( "  and at 500 nothing continues", !e.take_next( nxt + 500, LIMIT, seq, payload ) );
  check( "  in order up to 1,050, inside one held: it is taken, for its second half",
         e.take_next( nxt + 1050, LIMIT, seq, payload ) && seq == nxt + 1000 );
  /* in order up to 2,500: the segments at 1,100 and 2,000 are behind, and are thrown away; nothing continues */
  check( "  in order past two that are held: they are thrown away, and nothing is taken", !e.take_next( nxt + 2500, LIMIT, seq, payload ) );
  check( "  what is left is said from there: 3,000-3,100 and 4,000-4,100",
         ranges_of( e.blocks( nxt + 2500, LIMIT, 4 ), nxt ) == Ranges( { { 3000, 3100 }, { 4000, 4100 } } ) );

  Gosh::Early stale;
  stale.keep( nxt + 300, hundred, nxt, LIMIT );
  stale.keep( nxt + 3000, hundred, nxt, LIMIT );
  check( "  one that the next byte wanted has passed is not said to be held, though not yet thrown away",
         ranges_of( stale.blocks( nxt + 1000, LIMIT, 4 ), nxt ) == Ranges( { { 3000, 3100 } } ) );

  Gosh::Early full;
  full.keep( nxt + 200, std::string( LIMIT - 100, 'z' ), nxt, LIMIT );
  full.keep( nxt + 10, std::string( 101, 'z' ), nxt, LIMIT );
  check( "  no more is kept in all than the buffer is long", ranges_of( full.blocks( nxt, LIMIT, 4 ), nxt ).size() == 1 );
  full.keep( nxt + 10, std::string( 100, 'z' ), nxt, LIMIT );
  check( "  exactly that much is", ranges_of( full.blocks( nxt, LIMIT, 4 ), nxt ).size() == 2 );
  full.clear();
  check( "  cleared, it holds nothing", full.empty() && full.blocks( nxt, LIMIT, 4 ).empty() );


  /* (from the review of 2026-11-15: breaks these checks did not notice) */
  full.keep( nxt + 10, std::string( LIMIT, 'z' ), nxt, LIMIT );
  check( "  cleared, it has all its room again: a segment as long as the buffer is kept", !full.empty() );
  Gosh::Early room;
  room.keep( nxt + 100, std::string( LIMIT - 100, 'a' ), nxt, LIMIT );
  uint32_t got_seq = 0;
  std::string got_payload;
  check( "  (a nearly full store gives up its one segment)", room.take_next( nxt + 100, LIMIT, got_seq, got_payload ) && room.empty() );
  room.keep( nxt + LIMIT, std::string( LIMIT - 100, 'b' ), nxt + LIMIT - 100, LIMIT );
  check( "  and what it gave up no longer counts against its room", !room.empty() );

  Gosh::Early many;
  for ( int i = 0; i < 6; i++ ) {
    many.keep( nxt + 1000 * ( i + 1 ), hundred, nxt, LIMIT );
  }
  check( "  six ranges held and room for 146: four are said, and no more (the option must fit a header)",
         ranges_of( many.blocks( nxt, LIMIT, 146 ), nxt ).size() == 4 && many.blocks( nxt, LIMIT, 146 ).size() == 36 );
  check( "  the last to arrive began a range of its own: it is said first",
         ranges_of( many.blocks( nxt, LIMIT, 4 ), nxt )[0] == std::make_pair<uint32_t, uint32_t>( 6000, 6100 ) );
  Gosh::Early inside;
  inside.keep( nxt + 1000, std::string( 500, 'c' ), nxt, LIMIT );
  inside.keep( nxt + 1100, hundred, nxt, LIMIT ); /* wholly inside the first */
  check( "  a segment inside another does not shorten the range said", ranges_of( inside.blocks( nxt, LIMIT, 4 ), nxt ) == Ranges( { { 1000, 1500 } } ) );
  Gosh::Early edge;
  edge.keep( nxt + LIMIT, hundred, nxt, LIMIT );
  check( "  exactly as far ahead as the buffer is long: kept, and said", ranges_of( edge.blocks( nxt, LIMIT, 4 ), nxt ).size() == 1 );
  Gosh::Early last_byte;
  last_byte.keep( nxt + 100, hundred, nxt, LIMIT );
  check( "  in order up to its very end (nothing of it is new): thrown away, not handed back",
         !last_byte.take_next( nxt + 201, LIMIT, got_seq, got_payload ) && last_byte.empty() );
  Gosh::Early two_stale;
  two_stale.keep( nxt + 100, hundred, nxt, LIMIT );
  two_stale.keep( nxt + 300, hundred, nxt, LIMIT );
  two_stale.keep( nxt + 1000, hundred, nxt, LIMIT );
  check( "  two passed altogether and a third that continues: the third is handed back, the two gone",
         two_stale.take_next( nxt + 1000, LIMIT, got_seq, got_payload ) && got_seq == nxt + 1000 && two_stale.empty() );
}

int main()
{
  test_at( 1000000, "sequence numbers in the middle of their range:" );
  test_at( 0xFFFFF800u, "sequence numbers that wrap round within the data:" );

  if ( failures ) {
    printf( "FAIL REQ14 (selective acknowledgment): %d checks of segments that came early failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ14 (selective acknowledgment): segments that came early\n" );
  return EXIT_SUCCESS;
}
