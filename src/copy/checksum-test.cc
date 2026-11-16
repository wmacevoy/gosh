/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-2, REQ1/CP-3: the checksum (checksum.h): taken in pieces it is what it is taken whole, so
 * that a part held from before can be carried on from; it tells a changed byte; and its values are
 * pf.7's, worked out here independently (FNV-1a for the first half, which is a published function),
 * so that a gosh end and a pf.7 end agree on what arrived.
 */

#include <cstdint>
#include <cstdio>
#include <string>

#include "src/copy/checksum.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static FileCopy::Hash of( const std::string& s )
{
  FileCopy::Hash h;
  h.add( s.data(), s.size() );
  return h;
}

int main()
{
  using FileCopy::Hash;
  std::string big( 300000, '\0' );
  for ( size_t i = 0; i < big.size(); i++ ) {
    big[i] = static_cast<char>( ( i * 2654435761u ) >> 13 );
  }

  const Hash whole = of( big );
  Hash parts;
  parts.add( big.data(), 1000 );
  parts.add( big.data() + 1000, 0 );
  parts.add( big.data() + 1000, big.size() - 1000 );
  check( "taken in pieces, it is what it is taken whole (REQ1/CP-2: a part carried on from)", whole == parts );

  std::string changed = big;
  changed[150000] ^= 1;
  check( "one bit changed anywhere: another checksum (REQ1/CP-3)", !( of( changed ) == whole ) );
  check( "one byte fewer: another checksum", !( of( big.substr( 0, big.size() - 1 ) ) == whole ) );
  Hash half_differs = whole;
  half_differs.b ^= 1;
  check( "two checksums are the same only if both halves are", !( half_differs == whole ) );

  const Hash empty;
  check( "of nothing: the two starting values", empty.a == 0xcbf29ce484222325ULL && empty.b == 0x9e3779b97f4a7c15ULL );
  const Hash a = of( "a" );
  check( "of \"a\": FNV-1a's 0xaf63dc4c8601ec8c, and pf.7's second half",
         a.a == 0xaf63dc4c8601ec8cULL && a.b == 0xac4ae4d629f09a2aULL );
  const Hash m = of( "mocp-1" );
  check( "of \"mocp-1\": as worked out apart from this code",
         m.a == 0x4b6b0d5d046969d0ULL && m.b == 0x8ebd8586ca9731b9ULL );

  if ( failures ) {
    printf( "FAIL REQ1/CP-2, REQ1/CP-3: %d checks of the checksum failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/CP-2, REQ1/CP-3: the checksum\n" );
  return 0;
}
