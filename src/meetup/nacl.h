/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, nacl: the one thing in gosh that calls TweetNaCl (vendor/tweetnacl/). Logic: bytes
 * in, bytes out (but for the two functions that make keys, and random(), which draw from the
 * system and stop the program if they cannot).
 *
 * It hides what trips people up in the library: buffers that must begin with zeros, a
 * signature that comes joined to its message, sizes nobody checks. Every size is checked
 * here, and a wrong one is a "no", not a read past the end.
 *
 *   box        a message sealed from one key pair to another: only the two key holders can
 *              open it, and its opening shows the other holder sealed it. 16 bytes longer
 *              than the message. A nonce (24 bytes) must never repeat for one pair of keys.
 *   signature  64 bytes that show the holder of a signing key's private half signed a message.
 *   hash       SHA-512: 64 bytes.
 *
 * Wrong or lying input: a key, nonce or signature of the wrong length; a box too short to be
 * one; a box or signature that does not check: all false. A public key that makes the shared
 * secret all zeros (there are a few such; whoever sends one could be anybody): false.
 */

#ifndef GOSH_MEETUP_NACL_H
#define GOSH_MEETUP_NACL_H

#include <cstddef>
#include <string>

namespace Gosh {
namespace Nacl {

static const size_t KEY = 32;          /* a box key, either half; a signing key's public half */
static const size_t SIGNING_SECRET = 64; /* a signing key's private half */
static const size_t NONCE = 24;
static const size_t SIGNATURE = 64;
static const size_t HASH = 64;
static const size_t BOX_OVERHEAD = 16; /* how much longer a box is than its message */

struct Keys
{
  std::string pub, sec;
};

/* New key pairs, from the system's random bytes. */
Keys box_keys( void );
Keys signing_keys( void );

/* n random bytes from the system. Stops the program if they cannot be had. */
std::string random( size_t n );

/* Seals a message from my key pair to theirs. False (and `sealed` untouched) if a size is wrong or the keys give no secret. */
bool box( const std::string& message,
          const std::string& nonce,
          const std::string& their_pub,
          const std::string& my_sec,
          std::string& sealed );

/* Opens a box sealed to my key pair from theirs. False (and `message` untouched) if it does not open. */
bool box_open( const std::string& sealed,
               const std::string& nonce,
               const std::string& their_pub,
               const std::string& my_sec,
               std::string& message );

/* The signature of a message under a signing key's private half; empty if the key is the wrong length. */
std::string sign( const std::string& message, const std::string& sec );

/* Whether this is the signature of this message by the holder of this public half. */
bool verify( const std::string& signature, const std::string& message, const std::string& pub );

/* SHA-512. */
std::string hash( const std::string& message );

}
}

#endif
