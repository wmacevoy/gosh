/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ15/PATH-5: gosh's wrapper of TweetNaCl (nacl.h): that it seals and opens, signs and checks,
 * and says no to every wrong size and every degenerate key. (That the library itself gives the
 * published answers is vendor/tweetnacl/tweetnacl-test.c.)
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/meetup/nacl.h"

using namespace Gosh;

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static std::string from_hex( const char* hex )
{
  std::string out;
  for ( size_t i = 0; hex[i] && hex[i + 1]; i += 2 ) {
    unsigned int v = 0;
    sscanf( hex + i, "%2x", &v );
    out.push_back( static_cast<char>( v ) );
  }
  return out;
}

int main()
{
  const Nacl::Keys alice = Nacl::box_keys(), bob = Nacl::box_keys(), eve = Nacl::box_keys();
  check( "a box key pair is two halves of 32 bytes, and two pairs differ",
         alice.pub.size() == 32 && alice.sec.size() == 32 && alice.pub != bob.pub && alice.sec != bob.sec );
  check( "random bytes come in the length asked, and differ", Nacl::random( 24 ).size() == 24 && Nacl::random( 24 ) != Nacl::random( 24 )
                                                                && Nacl::random( 0 ).empty() );

  const std::string nonce = Nacl::random( Nacl::NONCE ), message = "meet me at the usual place";
  std::string sealed, opened = "untouched";
  check( "Alice seals a message to Bob: 16 bytes longer than the message",
         Nacl::box( message, nonce, bob.pub, alice.sec, sealed ) && sealed.size() == message.size() + 16 );
  check( "Bob opens it, knowing it is from Alice", Nacl::box_open( sealed, nonce, alice.pub, bob.sec, opened ) && opened == message );
  opened = "untouched";
  check( "Eve cannot open it", !Nacl::box_open( sealed, nonce, alice.pub, eve.sec, opened ) && opened == "untouched" );
  check( "nor does it open as if it were from Eve", !Nacl::box_open( sealed, nonce, eve.pub, bob.sec, opened ) );
  std::string other_nonce = nonce;
  other_nonce[0] ^= 1;
  check( "nor with another nonce", !Nacl::box_open( sealed, other_nonce, alice.pub, bob.sec, opened ) );
  bool any = false;
  for ( size_t i = 0; i < sealed.size(); i++ ) {
    std::string changed = sealed;
    changed[i] ^= 0x20;
    any = any || Nacl::box_open( changed, nonce, alice.pub, bob.sec, opened );
  }
  check( "nor with any one byte of it changed", !any );
  std::string empty_sealed;
  check( "a box of nothing is 16 bytes, and opens to nothing",
         Nacl::box( "", nonce, bob.pub, alice.sec, empty_sealed ) && empty_sealed.size() == 16
           && Nacl::box_open( empty_sealed, nonce, alice.pub, bob.sec, opened ) && opened.empty() );

  /* wrong sizes are a no, not a read past the end */
  std::string out = "untouched";
  check( "a nonce, or either key, of the wrong length: no box",
         !Nacl::box( message, nonce.substr( 1 ), bob.pub, alice.sec, out ) && !Nacl::box( message, nonce, bob.pub.substr( 1 ), alice.sec, out )
           && !Nacl::box( message, nonce, bob.pub, alice.sec + "x", out ) && out == "untouched" );
  check( "a box shorter than 16 bytes, or keys of the wrong length: does not open",
         !Nacl::box_open( sealed.substr( 0, 15 ), nonce, alice.pub, bob.sec, out ) && !Nacl::box_open( "", nonce, alice.pub, bob.sec, out )
           && !Nacl::box_open( sealed, nonce, alice.pub + "x", bob.sec, out ) && !Nacl::box_open( sealed, nonce, alice.pub, "", out ) );

  /* public keys that give everybody the same secret */
  const std::string zero( 32, '\0' );
  const std::string small_order = from_hex( "e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800" );
  check( "a public key of all zeros gives no secret: nothing is sealed to it or opened from it",
         !Nacl::box( message, nonce, zero, alice.sec, out ) && !Nacl::box_open( sealed, nonce, zero, bob.sec, out ) );
  check( "nor one of the few others that do the same", !Nacl::box( message, nonce, small_order, alice.sec, out ) );

  const Nacl::Keys signer = Nacl::signing_keys(), other = Nacl::signing_keys();
  check( "a signing key pair: 32 bytes public, 64 private", signer.pub.size() == 32 && signer.sec.size() == 64 );
  const std::string signature = Nacl::sign( message, signer.sec );
  check( "a signature is 64 bytes, and checks against the message and the signer's key",
         signature.size() == 64 && Nacl::verify( signature, message, signer.pub ) );
  check( "not against another message, another key, or with a byte changed",
         !Nacl::verify( signature, message + "!", signer.pub ) && !Nacl::verify( signature, message, other.pub )
           && !Nacl::verify( std::string( 1, signature[0] ^ 1 ) + signature.substr( 1 ), message, signer.pub ) );
  check( "the signature of nothing checks against nothing", Nacl::verify( Nacl::sign( "", signer.sec ), "", signer.pub ) );
  check( "a signature or key of the wrong length does not check; a private half of the wrong length signs nothing",
         !Nacl::verify( signature.substr( 1 ), message, signer.pub ) && !Nacl::verify( signature, message, signer.pub.substr( 1 ) )
           && Nacl::sign( message, signer.sec.substr( 1 ) ).empty() );
  /* RFC 8032, test 1: a private half is its 32-byte seed and then the public half */
  const std::string rfc_pub = from_hex( "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a" );
  const std::string rfc_sec = from_hex( "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60" ) + rfc_pub;
  check( "through the wrapper, RFC 8032's first signature comes out as published",
         Nacl::sign( "", rfc_sec ) == from_hex( "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                                               "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" ) );

  check( "through the wrapper, SHA-512 of \"abc\" is as published",
         Nacl::hash( "abc" ) == from_hex( "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                                         "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f" ) );

  if ( failures ) {
    printf( "FAIL REQ15/PATH-5: %d checks of gosh's wrapper of TweetNaCl failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ15/PATH-5: gosh's wrapper of TweetNaCl\n" );
  return EXIT_SUCCESS;
}
