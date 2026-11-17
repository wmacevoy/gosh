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
#include "vendor/facts/facts.h"



static FileCopy::Hash of( const std::string& s )
{
  FileCopy::Hash h;
  h.add( s.data(), s.size() );
  return h;
}
/* REQ1/CP-2, REQ1/CP-3: the checksum */
FACTS( Checksum )
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
  FACT_CHECK( ( whole == parts ), "taken in pieces, it is what it is taken whole (REQ1/CP-2: a part carried on from)" );

  std::string changed = big;
  changed[150000] ^= 1;
  FACT_CHECK( !( of( changed ) == whole ), "one bit changed anywhere: another checksum (REQ1/CP-3)" );
  FACT_CHECK( !( of( big.substr( 0, big.size() - 1 ) ) == whole ), "one byte fewer: another checksum" );
  Hash half_differs = whole;
  half_differs.b ^= 1;
  FACT_CHECK( !( half_differs == whole ), "two checksums are the same only if both halves are" );

  const Hash empty;
  FACT_CHECK( ( empty.a == 0xcbf29ce484222325ULL && empty.b == 0x9e3779b97f4a7c15ULL ), "of nothing: the two starting values" );
  const Hash a = of( "a" );
  FACT_CHECK( ( a.a == 0xaf63dc4c8601ec8cULL && a.b == 0xac4ae4d629f09a2aULL ), "of \"a\": FNV-1a's 0xaf63dc4c8601ec8c, and pf.7's second half" );
  const Hash m = of( "mocp-1" );
  FACT_CHECK( ( m.a == 0x4b6b0d5d046969d0ULL && m.b == 0x8ebd8586ca9731b9ULL ), "of \"mocp-1\": as worked out apart from this code" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Checksum );
}

FACTS_MAIN
