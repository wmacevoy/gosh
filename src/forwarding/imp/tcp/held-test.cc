/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ14 (selective acknowledgment): what the other end says it holds (held.h), with numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/imp/tcp/held.h"
#include "vendor/facts/facts.h"

/* An acknowledgment's option naming these ranges (kind 5), as sequence numbers. */
static std::string says( const std::vector<std::pair<uint32_t, uint32_t> >& ranges )
{
  std::string option;
  option.push_back( 1 );
  option.push_back( 1 );
  option.push_back( 5 );
  option.push_back( static_cast<char>( 2 + 8 * ranges.size() ) );
  for ( size_t i = 0; i < ranges.size(); i++ ) {
    Gosh::Forwarding::Imp::SegmentOptions::put32( option, ranges[i].first );
    Gosh::Forwarding::Imp::SegmentOptions::put32( option, ranges[i].second );
  }
  return option;
}

static void test_at( uint32_t una, const char* where )
{
  Facts* facts = FactsCurrent(); /* the group running */
  printf( "%s\n", where );
  Gosh::Forwarding::Imp::Held h;
  uint32_t at = 0, end = 0;
  FACT_CHECK( ( h.empty() && !h.next_gap( una, una, at, end ) && h.below( una, 5000 ) == 0 ), "  nothing held: no gap to fill, nothing to pass over" );

  /* 10,000 bytes sent. It says it holds 2,000 to 3,000 and 5,000 to 6,000. */
  h.note( says( { { una + 5000, una + 6000 }, { una + 2000, una + 3000 } } ), una, 10000 );
  FACT_CHECK( ( h.below( una, 10000 ) == 2000 && h.below( una, 2500 ) == 500 && h.below( una, 2000 ) == 0 ), "  two ranges told of, in either order: 2,000 held below 10,000; 500 below 2,500; none below 2,000" );
  FACT_CHECK( ( h.next_gap( una, una, at, end ) && at == 0 && end == 2000 ), "  the first gap is 0 to 2,000" );
  FACT_CHECK( ( h.next_gap( una, una + 2000, at, end ) && at == 3000 && end == 5000 ), "  from 2,000 on, the next gap is 3,000 to 5,000" );
  FACT_CHECK( ( h.next_gap( una, una + 4000, at, end ) && at == 4000 && end == 5000 ), "  from 4,000 on, it is 4,000 to 5,000" );
  FACT_CHECK( !h.next_gap( una, una + 5500, at, end ), "  beyond the last range held there is no gap: it may not have arrived yet" );

  uint32_t nxt = una + 2500;
  FACT_CHECK( ( h.pass_over( una, nxt ) == 2000 && nxt == una + 3000 ), "  sending again from inside a range: passed over to its end, with 2,000 to go before the next" );
  nxt = una + 500;
  FACT_CHECK( ( h.pass_over( una, nxt ) == 1500 && nxt == una + 500 ), "  from before a range: not moved, with 1,500 to go before it" );
  nxt = una + 7000;
  FACT_CHECK( ( h.pass_over( una, nxt ) == 0xFFFFFFFF && nxt == una + 7000 ), "  from beyond them all: not moved, and nothing ahead" );

  /* ranges that touch or overlap become one */
  h.note( says( { { una + 3000, una + 5000 } } ), una, 10000 );
  FACT_CHECK( ( h.below( una, 10000 ) == 4000 && h.next_gap( una, una + 2000, at, end ) == false ), "  told of what lies between two ranges: they are one, 2,000 to 6,000" );
  h.note( says( { { una + 5500, una + 7000 } } ), una, 10000 );
  FACT_CHECK( h.below( una, 10000 ) == 5000, "  told of a range that overlaps: it grows to 7,000, counted once" );

  /* what is not believed */
  Gosh::Forwarding::Imp::Held k;
  k.note( says( { { una, una + 1000 } } ), una, 10000 );
  FACT_CHECK( k.empty(), "  a range that begins at what is not yet acknowledged is not believed" );
  k.note( says( { { una + 9000, una + 10001 } } ), una, 10000 );
  FACT_CHECK( k.empty(), "  nor one that ends past what was sent" );
  k.note( says( { { una + 3000, una + 3000 } } ), una, 10000 );
  k.note( says( { { una + 4000, una + 3000 } } ), una, 10000 );
  k.note( says( { { una - 500, una + 300 } } ), una, 10000 );
  FACT_CHECK( k.empty(), "  nor an empty one, one that runs backwards, or one from before" );
  std::string cut = says( { { una + 1000, una + 2000 } } );
  k.note( cut.substr( 0, cut.size() - 1 ), una, 10000 );
  FACT_CHECK( k.empty(), "  nor an option cut short" );
  k.note( says( { { una + 9000, una + 10000 } } ), una, 10000 );
  FACT_CHECK( ( !k.empty() && k.below( una, 10000 ) == 1000 ), "  one that ends exactly where what was sent ends is" );

  /* acknowledgment moves on */
  h.forget( una + 1000 );
  FACT_CHECK( ( h.below( una + 1000, 9000 ) == 5000 && h.next_gap( una + 1000, una + 1000, at, end ) && at == 0 && end == 1000 ), "  acknowledged to 1,000: the range (now 1,000 to 6,000 from there) is kept" );
  h.forget( una + 4000 );
  FACT_CHECK( h.empty(), "  acknowledged to 4,000, inside the range: it was never held, and is forgotten" );


  /* (from the review of 2026-11-15: breaks these checks did not notice) */
  Gosh::Forwarding::Imp::Held g;
  g.add( una, 2000, 3000 );
  uint32_t exactly = una + 2000;
  FACT_CHECK( ( g.pass_over( una, exactly ) == 0xFFFFFFFF && exactly == una + 3000 ), "  sending again from exactly where a range begins: passed over to its end" );
  g.add( una, 3000, 4000 ); /* touches the first: one range, 2,000 to 4,000 */
  g.forget( una + 2500 );
  FACT_CHECK( g.empty(), "  two ranges that touch are one: acknowledged into it, the whole of it is forgotten" );
  Gosh::Forwarding::Imp::Held far;
  far.add( una, 5000, 6000 );
  far.forget( una + 1000 );
  FACT_CHECK( far.below( una + 1000, 9000 ) == 1000, "  a range ahead is kept when acknowledgment moves, wherever the numbers wrap" );
  far.forget( una + 5000 );
  FACT_CHECK( far.empty(), "  one that begins exactly at what is now acknowledged is forgotten" );
  Gosh::Forwarding::Imp::Held behind;
  behind.add( una + 3000, 1000, 2000 );
  FACT_CHECK( ( behind.next_gap( una + 3000, una + 1000, at, end ) && at == 0 && end == 1000 ), "  asked for a gap from before what is acknowledged: the gap begins at what is acknowledged" );

  /* options are a list; a range is taken only from a well-formed kind 5 */
  const std::string one = says( { { una + 1000, una + 2000 } } );
  Gosh::Forwarding::Imp::Held n1, n2, n3, n4, n5;
  n1.note( std::string( "\x01\x01", 2 ) + one.substr( 2 ) + one.substr( 2 ), una, 10000 );
  FACT_CHECK( n1.below( una, 10000 ) == 1000, "  padding before the option is passed over" );
  n2.note( std::string( 1, '\0' ) + one.substr( 2 ), una, 10000 );
  FACT_CHECK( n2.empty(), "  after the end of the list (kind 0) nothing is read" );
  n3.note( std::string( "\x09\x03x", 3 ) + one.substr( 2 ), una, 10000 );
  FACT_CHECK( n3.below( una, 10000 ) == 1000, "  an unknown kind is passed over by its length, and what follows is read" );
  std::string odd = one.substr( 2 );
  odd[1] = 11; /* a length that is not a head and whole ranges */
  n4.note( odd + "x", una, 10000 );
  FACT_CHECK( n4.empty(), "  a kind 5 whose length is not whole ranges is not read" );
  n5.note( std::string( "\x09\x01", 2 ) + one.substr( 2 ), una, 10000 );
  FACT_CHECK( n5.empty(), "  a length that cannot be (less than 2) ends the reading, and does not go round for ever" );
}
/* REQ14 (selective acknowledgment): what the other end holds */
FACTS( Held )
{
  test_at( 1000000, "sequence numbers in the middle of their range:" );
  test_at( 0xFFFFF000u, "sequence numbers that wrap round within the data:" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Held );
}

FACTS_MAIN
