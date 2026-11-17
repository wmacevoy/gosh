/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/FWD-2: what arrives, made into whole datagrams (receiving.h), with numbers for time. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/udp/receiving.h"
#include "vendor/facts/facts.h"



static std::string pattern( size_t length, int salt )
{
  std::string s( length, 0 );
  for ( size_t i = 0; i < length; i++ ) {
    s[i] = static_cast<char>( ( ( i + salt ) * 2654435761u ) >> 11 );
  }
  return s;
}

/* A datagram of 1,000 bytes in fragments of at most 200: six pieces; with parity, a seventh. */
static std::vector<std::string> pieces( const std::string& datagram, bool parity )
{
  return Gosh::DatagramAssembly::split( datagram, 200, parity );
}
/* REQ1/FWD-2: a UDP forward's receiving, with no socket and no clock */
FACTS( Receiving )
{
  using Gosh::Receiving;
  std::string got;

  {
    Receiving r;
    FACT_CHECK( ( r.fresh( 5000 ) && !r.fresh( 5000 ) ), "a number is new once" );
    FACT_CHECK( ( r.fresh( 4000 ) && !r.fresh( 5000 - 1024 ) && r.fresh( 5000 - 1023 ) ), "an earlier one not yet seen is new; 1,024 behind the newest is too old to tell" );
  }
  {
    Receiving r;
    const std::string a = pattern( 1000, 1 );
    const std::vector<std::string> six = pieces( a, false );
    bool early = false;
    for ( size_t i = 0; i + 1 < six.size(); i++ ) {
      early = early || r.fragment( 0, 7, 100 + i, six[i], 1000, got );
    }
    FACT_CHECK( six.size() == 6, "(six pieces)" );
    FACT_CHECK( ( !early && r.partial_count() == 1 ), "five of six fragments: no datagram yet, one in pieces" );
    FACT_CHECK( ( r.fragment( 0, 7, 105, six[5], 1000, got ) && got == a && r.get_reassembled() == 1
                                               && r.partial_count() == 0 ), "the sixth: the datagram, whole" );

    const std::string b = pattern( 1000, 2 );
    const std::vector<std::string> seven = pieces( b, true );
    bool done = false;
    for ( size_t i = seven.size(); i-- > 0; ) { /* backwards, and the third never comes */
      if ( i != 2 ) {
        done = r.fragment( 0, 7, 200 + i, seven[i], 1000, got ) || done;
      }
    }
    FACT_CHECK( ( seven.size() == 7 && done && got == b && r.get_recovered() == 1 ), "seven with parity, backwards, one lost: whole, rebuilt with the parity" );
    FACT_CHECK( ( !r.fragment( 0, 7, 202, seven[2], 1000, got ) && r.partial_count() == 0 ), "the lost one turning up late starts nothing" );

    const std::vector<std::string> c = pieces( pattern( 1000, 3 ), false );
    r.fragment( 0, 7, 300, c[0], 2000, got );
    bool completed = false;
    for ( size_t k = 1; k < c.size(); k++ ) {
      completed = r.fragment( k % 2 ? 0 : 1, k % 2 ? 8 : 7, 300 + k, c[k], 2000, got ) || completed;
    }
    FACT_CHECK( ( !completed && r.partial_count() == 1 ), "its other fragments, said to be of another conversation or another forward, do not complete it" );
    FACT_CHECK( ( !r.fragment( 0, 7, 400, "x", 2000, got ) && !r.fragment( 0, 7, 500, std::string( "\x09\x02\x00\x0a" "data", 8 ), 2000, got )
             && r.partial_count() == 1 ), "what makes no sense as a fragment holds no place" );

    const std::vector<std::string> d = pieces( pattern( 1000, 4 ), false );
    r.fragment( 0, 7, 600, d[0], 3000, got ); /* exactly a second after c's first */
    FACT_CHECK( ( r.partial_count() == 2 && r.get_abandoned() == 0 ), "a second after its first fragment a datagram in pieces is still waited for" );
    r.fragment( 0, 7, 601, d[1], 3001, got );
    FACT_CHECK( ( r.partial_count() == 1 && r.get_abandoned() == 1 ), "a millisecond later it is abandoned" );
  }
  {
    /* Sixty-four datagrams in pieces at once; the sixty-fifth pushes out the oldest. */
    Receiving r;
    const std::vector<std::string> p = pieces( pattern( 1000, 5 ), false );
    for ( int i = 0; i < 64; i++ ) {
      r.fragment( 0, 7, 1000 + 10 * i, p[0], 5000, got );
    }
    FACT_CHECK( ( r.partial_count() == 64 && r.get_abandoned() == 0 ), "(sixty-four in pieces)" );
    r.fragment( 0, 7, 1000 + 10 * 64, p[0], 5000, got );
    FACT_CHECK( ( r.partial_count() == 64 && r.get_abandoned() == 1 ), "a sixty-fifth: still sixty-four, one abandoned" );
    bool finished = false;
    for ( size_t k = 1; k < p.size(); k++ ) {
      finished = r.fragment( 0, 7, 1000 + k, p[k], 5000, got ) || finished;
    }
    FACT_CHECK( !finished, "and it was the oldest: its other fragments no longer complete it" );
  }
  {
    /* The last 256 rebuilt are remembered, so that a straggler does not start one of them again. */
    Receiving r;
    const std::vector<std::string> p = pieces( pattern( 1000, 6 ), true ); /* seven; six complete it */
    for ( int n = 0; n < 257; n++ ) {
      for ( size_t k = 0; k < 6; k++ ) {
        r.fragment( 0, 7, 10000 + 10 * n + k, p[k], 5000, got );
      }
    }
    FACT_CHECK( ( r.get_reassembled() == 257 && r.partial_count() == 0 ), "(257 datagrams rebuilt, each with its parity fragment still to come)" );
    r.fragment( 0, 7, 10000 + 10 * 1 + 6, p[6], 5000, got );
    FACT_CHECK( r.partial_count() == 0, "the parity of the second, late: ignored" );
    r.fragment( 0, 7, 10000 + 10 * 0 + 6, p[6], 5000, got );
    FACT_CHECK( r.partial_count() == 1, "the parity of the first, the 257th back: no longer remembered, and starts a datagram" );
  }

  /* (from the review of 2026-11-15: breaks these checks did not notice) */
  {
    Receiving r;
    const std::string a = pattern( 1000, 9 );
    const std::vector<std::string> six = pieces( a, false );
    r.fragment( 3, 7, 900, six[0], 1000, got );
    bool wrong_conversation = false, wrong_forward = false;
    for ( size_t k = 1; k < six.size(); k++ ) {
      wrong_conversation = r.fragment( 3, 8, 900 + k, six[k], 1000, got ) || wrong_conversation;
    }
    for ( size_t k = 1; k < six.size(); k++ ) {
      wrong_forward = r.fragment( 4, 7, 900 + k, six[k], 1000, got ) || wrong_forward;
    }
    FACT_CHECK( !wrong_conversation, "all its other fragments said to be of another conversation: it is not completed" );
    FACT_CHECK( !wrong_forward, "all of them said to be of another forward (the first was of forward 3): nor then" );
    bool done = false;
    for ( size_t k = 1; k < six.size(); k++ ) {
      done = r.fragment( 3, 7, 900 + k, six[k], 1000, got ) || done;
    }
    FACT_CHECK( ( done && got == a ), "said truly, they complete it" );

    const std::vector<std::string> other = pieces( pattern( 1000, 10 ), false );
    r.fragment( 0, 7, 2000, other[0], 1000, got );
    r.fragment( 0, 7, 2000 + 9, std::string( "\x09\x02\x00\x0a" "data", 8 ), 1000, got ); /* junk that claims this datagram */
    bool still = false;
    for ( size_t k = 1; k < other.size(); k++ ) {
      still = r.fragment( 0, 7, 2000 + k, other[k], 1000, got ) || still;
    }
    FACT_CHECK( still, "junk that claims a datagram already in pieces does not lose the pieces" );
  }
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Receiving );
}

FACTS_MAIN
