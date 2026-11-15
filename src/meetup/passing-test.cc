/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-5: a meetup passing datagrams on (passing.h), with numbers for time and the real
 * proof of the key, and no socket. Its last test replays a proof it has seen and says what
 * happens (queue item 8; docs/FINDINGS.md, 2026-11-13).
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/crypto/crypto.h"
#include "src/meetup/passing.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-94s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static const std::string KEY = Crypto::Base64Key().printable_key();
static unsigned int asked = 0; /* how many times a proof had to be worked out */

static bool good( const char* twelve )
{
  asked++;
  return Network::meetup_proof_ok( twelve, KEY );
}

/* A datagram for passing on: the mark, a proof, the pair's name (16 bytes), and what is passed. */
static std::string wrapped( const std::string& proof, const std::string& pair, const std::string& inside )
{
  std::string name = pair;
  name.resize( 16, ' ' );
  return std::string( Network::MEETUP_MARK, 8 ) + proof + name + inside;
}

/* Whether this datagram from this address at this time is passed on, and to whom ("" if not). */
static std::string passed( Gosh::Passing& p, const std::string& datagram, const std::string& from, uint64_t now )
{
  std::string to;
  return p.datagram( datagram.data(), datagram.size(), from, now, good, to ) ? to : "";
}

static const uint64_t T = 1700000000; /* seconds */

static void test_pairs( void )
{
  Gosh::Passing p;
  const std::string proof = Network::meetup_proof( KEY );
  const std::string d = wrapped( proof, "pair one", "sealed" );

  check( "the first end of a pair to be heard: nobody to pass it to yet", passed( p, d, "A", T ) == "" && p.pair_count() == 1 );
  check( "the second end: passed on to the first", passed( p, d, "B", T + 1 ) == "A" );
  check( "and the first's are passed to the second", passed( p, d, "A", T + 2 ) == "B" );
  check( "the proof was worked out once, and remembered", asked == 1 );
  check( "another pair's datagram does not go to this pair's ends",
         passed( p, wrapped( proof, "pair two", "sealed" ), "C", T + 2 ) == "" && p.pair_count() == 2 );

  /* an end that moves: a third address naming the pair takes the place of whichever end has been quiet longest */
  check( "a third address naming the pair takes the place of the end quiet longest (B), and is passed to the other",
         passed( p, d, "B moved", T + 3 ) == "A" && passed( p, d, "A", T + 4 ) == "B moved" );

  check( "an end not heard for 120 s is still passed to", passed( p, d, "A", T + 3 + 120 ) == "B moved" );
  check( "for 121 s it is not", passed( p, d, "A", T + 3 + 121 ) == "" );

  /* neither end for two minutes: the pair is forgotten (looked for every 30 s, when a datagram comes) */
  Gosh::Passing q;
  passed( q, d, "A", T );
  passed( q, wrapped( proof, "pair two", "x" ), "C", T + 150 );
  check( "a pair neither of whose ends has been heard for two minutes is forgotten", q.pair_count() == 1 );
}

static void test_refused( void )
{
  Gosh::Passing p;
  const std::string proof = Network::meetup_proof( KEY );
  const std::string d = wrapped( proof, "pair", "sealed" );
  passed( p, d, "A", T );

  check( "a datagram shorter than the header is not for passing on", passed( p, d.substr( 0, 35 ), "B", T ) == "" );
  check( "one of exactly the header's 36 bytes, passing nothing, is", passed( p, d.substr( 0, 36 ), "B", T ) == "A" );
  std::string unmarked = d;
  unmarked[3] ^= 1;
  check( "one without the mark is not", passed( p, unmarked, "B", T ) == "" );

  const std::string wrong = Network::meetup_proof( Crypto::Base64Key().printable_key() );
  Gosh::Passing fresh;
  passed( fresh, d, "A", T );
  check( "a proof made with another key: not passed on, and the sender is not taken for an end of the pair",
         passed( fresh, wrapped( wrong, "pair", "sealed" ), "B", T ) == "" && passed( fresh, d, "A", T + 1 ) == "" );

  /* 4,096 pairs and no more */
  Gosh::Passing full;
  for ( int i = 0; i < 4096; i++ ) {
    passed( full, wrapped( proof, std::to_string( i ), "" ), "X", T );
  }
  passed( full, wrapped( proof, "one too many", "" ), "Y", T );
  check( "4,096 pairs are held, and a 4,097th is refused", full.pair_count() == 4096 );
}

/*
 * The finding. An onlooker on the path sees one datagram go to the meetup. They do not have the
 * key. They keep the twelve bytes of proof at its front.
 */
static void test_replay( void )
{
  printf( "replaying a proof that was seen (docs/FINDINGS.md, 2026-11-13):\n" );
  Gosh::Passing p;
  const std::string theirs = wrapped( Network::meetup_proof( KEY ), "alice and bob", "sealed" );
  passed( p, theirs, "alice", T );
  passed( p, theirs, "bob", T + 1 );
  const std::string seen = theirs.substr( 8, 12 ); /* what the onlooker keeps */

  /* A day later, from two addresses of their own, for a pair of their own naming. */
  const std::string own = wrapped( seen, "mallory's own", "anything at all" );
  passed( p, own, "mallory 1", T + 86400 );
  check( "  with it, a day later, the onlooker has a pair of their own naming passed on: the meetup is theirs to use",
         passed( p, own, "mallory 2", T + 86401 ) == "mallory 1" );

  /* And into the pair they saw: they take an end's place, and what the other end sends comes to them. */
  passed( p, theirs, "alice", T + 86500 );
  passed( p, theirs, "bob", T + 86501 );
  const std::string into = wrapped( seen, "alice and bob", "not sealed by either end" );
  check( "  naming the pair they saw, their datagram is passed on to one of its ends (which will refuse it, unsealed)",
         passed( p, into, "mallory 1", T + 86502 ) == "bob" );
  check( "  and they have taken the other end's place: what that pair's other end sends next comes to the onlooker",
         passed( p, theirs, "bob", T + 86503 ) == "mallory 1" );
  check( "  until the end that was put out sends again, and takes a place back",
         passed( p, theirs, "alice", T + 86504 ) != "" );
  printf( "  (so: whoever sees one datagram can use the meetup for good, and can keep putting an end of a pair out of it.\n"
          "   They cannot read or change what the ends seal. Not fixed: it is the wire's, and the owner's to decide.)\n" );
}

int main()
{
  test_pairs();
  test_refused();
  test_replay();

  if ( failures ) {
    printf( "FAIL REQ1/PATH-5: %d checks of a meetup's passing on failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/PATH-5: a meetup's passing on, with no socket and no clock; and a seen proof replayed\n" );
  return EXIT_SUCCESS;
}
