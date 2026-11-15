/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

#include "src/crypto/prng.h"
#include "src/meetup/nacl.h"

extern "C" {
#include "src/tweetnacl/tweetnacl.h"

/*
 * TweetNaCl asks whoever links it for random bytes. If the system has none to give there is
 * no safe way on: a key made from anything else is not a key. (An edge: this is the only
 * place the part reads the system's randomness.)
 */
void randombytes( unsigned char* bytes, unsigned long long count )
{
  try {
    PRNG().fill( bytes, count );
  } catch ( const std::exception& e ) {
    fputs( "gosh: no random bytes are to be had from this system; stopping.\n", stderr );
    abort();
  }
}
}

using namespace Gosh;

static const unsigned char* u( const std::string& s )
{
  return reinterpret_cast<const unsigned char*>( s.data() );
}

Nacl::Keys Nacl::box_keys( void )
{
  unsigned char pub[crypto_box_PUBLICKEYBYTES], sec[crypto_box_SECRETKEYBYTES];
  crypto_box_keypair( pub, sec );
  Keys keys;
  keys.pub.assign( reinterpret_cast<char*>( pub ), sizeof( pub ) );
  keys.sec.assign( reinterpret_cast<char*>( sec ), sizeof( sec ) );
  return keys;
}

Nacl::Keys Nacl::signing_keys( void )
{
  unsigned char pub[crypto_sign_PUBLICKEYBYTES], sec[crypto_sign_SECRETKEYBYTES];
  crypto_sign_keypair( pub, sec );
  Keys keys;
  keys.pub.assign( reinterpret_cast<char*>( pub ), sizeof( pub ) );
  keys.sec.assign( reinterpret_cast<char*>( sec ), sizeof( sec ) );
  return keys;
}

std::string Nacl::random( size_t n )
{
  std::string bytes( n, '\0' );
  if ( n ) {
    randombytes( reinterpret_cast<unsigned char*>( &bytes[0] ), n );
  }
  return bytes;
}

/* What the two keys agree on, ready for sealing; false if a size is wrong or the agreement is all zeros. */
static bool agreed( const std::string& their_pub, const std::string& my_sec, unsigned char* k )
{
  if ( their_pub.size() != Nacl::KEY || my_sec.size() != Nacl::KEY ) {
    return false;
  }
  unsigned char shared[crypto_scalarmult_BYTES];
  crypto_scalarmult( shared, u( my_sec ), u( their_pub ) );
  unsigned char any = 0;
  for ( size_t i = 0; i < sizeof( shared ); i++ ) {
    any |= shared[i];
  }
  if ( !any ) {
    return false; /* a degenerate public key: anybody could have sent it */
  }
  return crypto_box_beforenm( k, u( their_pub ), u( my_sec ) ) == 0;
}

bool Nacl::box( const std::string& message,
                const std::string& nonce,
                const std::string& their_pub,
                const std::string& my_sec,
                std::string& sealed )
{
  unsigned char k[crypto_box_BEFORENMBYTES];
  if ( nonce.size() != NONCE || !agreed( their_pub, my_sec, k ) ) {
    return false;
  }
  /* (the library wants 32 zero bytes before the message, and gives 16 zero bytes before the box) */
  std::vector<unsigned char> in( crypto_box_ZEROBYTES + message.size(), 0 ), out( in.size(), 0 );
  memcpy( in.data() + crypto_box_ZEROBYTES, message.data(), message.size() );
  if ( crypto_box_afternm( out.data(), in.data(), in.size(), u( nonce ), k ) != 0 ) {
    return false;
  }
  sealed.assign( reinterpret_cast<char*>( out.data() ) + crypto_box_BOXZEROBYTES, out.size() - crypto_box_BOXZEROBYTES );
  return true;
}

bool Nacl::box_open( const std::string& sealed,
                     const std::string& nonce,
                     const std::string& their_pub,
                     const std::string& my_sec,
                     std::string& message )
{
  unsigned char k[crypto_box_BEFORENMBYTES];
  if ( nonce.size() != NONCE || sealed.size() < BOX_OVERHEAD || !agreed( their_pub, my_sec, k ) ) {
    return false;
  }
  std::vector<unsigned char> in( crypto_box_BOXZEROBYTES + sealed.size(), 0 ), out( in.size(), 0 );
  memcpy( in.data() + crypto_box_BOXZEROBYTES, sealed.data(), sealed.size() );
  if ( crypto_box_open_afternm( out.data(), in.data(), in.size(), u( nonce ), k ) != 0 ) {
    return false;
  }
  message.assign( reinterpret_cast<char*>( out.data() ) + crypto_box_ZEROBYTES, out.size() - crypto_box_ZEROBYTES );
  return true;
}

std::string Nacl::sign( const std::string& message, const std::string& sec )
{
  if ( sec.size() != SIGNING_SECRET ) {
    return std::string();
  }
  /* (the library gives the signature joined to the front of the message) */
  std::vector<unsigned char> joined( SIGNATURE + message.size() );
  unsigned long long length = 0;
  crypto_sign( joined.data(), &length, u( message ), message.size(), u( sec ) );
  return std::string( reinterpret_cast<char*>( joined.data() ), SIGNATURE );
}

bool Nacl::verify( const std::string& signature, const std::string& message, const std::string& pub )
{
  if ( signature.size() != SIGNATURE || pub.size() != KEY ) {
    return false;
  }
  const std::string joined = signature + message;
  std::vector<unsigned char> opened( joined.size() ); /* (it wants room for the whole of what it was given) */
  unsigned long long length = 0;
  return crypto_sign_open( opened.data(), &length, u( joined ), joined.size(), u( pub ) ) == 0;
}

std::string Nacl::hash( const std::string& message )
{
  unsigned char out[crypto_hash_BYTES];
  crypto_hash( out, u( message ), message.size() );
  return std::string( reinterpret_cast<char*>( out ), sizeof( out ) );
}
