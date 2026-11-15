/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <cstdint>

#include "src/crypto/crypto.h"
#include "src/crypto/prng.h"
#include "src/meetup/proof.h"

using namespace Network;

const char Network::MEETUP_MARK[8] = { 0, 'M', 'O', 'S', 'H', 'F', 'W', 'D' };

/* The eight bytes that the key gives for four. */
static std::string proof_mark( const char* four, const std::string& key )
{
  uint64_t n = 0;
  for ( int i = 0; i < 4; i++ ) {
    n = ( n << 8 ) | static_cast<unsigned char>( four[i] );
  }
  Crypto::Session session( ( Crypto::Base64Key( key ) ) );
  /* (sealing nothing, under a number: what comes back is the number and a tag that only the key makes) */
  const std::string sealed = session.encrypt( Crypto::Message( Crypto::Nonce( n ), "" ) );
  return sealed.substr( sealed.size() - 8 );
}

std::string Network::meetup_proof( const std::string& key )
{
  char four[4];
  PRNG().fill( four, sizeof( four ) );
  return std::string( four, 4 ) + proof_mark( four, key );
}

bool Network::meetup_proof_ok( const char* txid, const std::string& key )
{
  const std::string mark = proof_mark( txid, key );
  unsigned char difference = 0;
  for ( size_t i = 0; i < 8; i++ ) {
    difference |= static_cast<unsigned char>( mark[i] ^ txid[4 + i] );
  }
  return difference == 0;
}
