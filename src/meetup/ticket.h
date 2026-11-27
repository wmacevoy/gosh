/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, ticket: what lets one session's two ends use a meetup (REQ18/PATH-5; the proposal,
 * revision 10, section 3). Logic: bytes in, bytes out; no clock, no randomness, no file.
 *
 * A ticket is an id and a key. The id is 8 bytes the maker chose at random and the instant it
 * stops being good (UTC seconds, 8 bytes, most significant first). The key is the master key's
 * keyed permutation of the id, so a meetup keeps nothing per ticket: it computes the key again
 * from the id. Whoever can log in to the meetup's machine is given tickets over ssh; nothing else
 * makes one.
 *
 * The permutation is mosh's own cipher (AES-OCB) sealing the 16-byte id under the master key with
 * the nonce fixed at zero. For one full block OCB is a fixed mask, AES, and the same mask again: a
 * keyed permutation of the block, which is what deriving a key from an id needs. The fixed nonce
 * is safe only because the master key seals nothing else, ever.
 *
 * Wrong input: an id or key not 16 bytes, or text that is not two keys, is refused (false, or an
 * empty key), never cut or padded.
 */

#ifndef GOSH_MEETUP_TICKET_H
#define GOSH_MEETUP_TICKET_H

#include <cstdint>
#include <string>

#include "src/crypto/imp/base64.h" /* (the cipher's inside: seam/DOMAINS allows it, until its ABI makes a key of 16 bytes) */
#include "src/crypto/crypto.h"

namespace Gosh::Meetup {

static const size_t TICKET_ID_LEN = 16;
static const size_t TICKET_KEY_LEN = 16;
static const uint64_t TICKET_LIFE = 30 * 24 * 3600; /* seconds: how long a ticket is made to be good for */

/* A 16-byte key as mosh's cipher takes it. Throws Crypto::CryptoException if it is not 16 bytes. */
inline Crypto::Base64Key raw_key( const std::string& sixteen )
{
  if ( sixteen.size() != 16 ) {
    throw Crypto::CryptoException( "a key is 16 bytes" );
  }
  char b64[24];
  Crypto::Imp::base64_encode( reinterpret_cast<const uint8_t*>( sixteen.data() ), 16, b64, sizeof( b64 ) );
  return Crypto::Base64Key( std::string( b64, 22 ) );
}

/* When the ticket with this id stops being good (UTC seconds); 0 for what is not an id. */
inline uint64_t ticket_until( const std::string& id )
{
  if ( id.size() != TICKET_ID_LEN ) {
    return 0;
  }
  uint64_t until = 0;
  for ( size_t i = 8; i < 16; i++ ) {
    until = ( until << 8 ) | static_cast<unsigned char>( id[i] );
  }
  return until;
}

inline bool ticket_good_at( const std::string& id, uint64_t now )
{
  return now < ticket_until( id );
}

/* The key of the ticket with this id, under this master key; empty if either is the wrong size. */
inline std::string ticket_key( const std::string& master, const std::string& id )
{
  if ( master.size() != TICKET_KEY_LEN || id.size() != TICKET_ID_LEN ) {
    return std::string();
  }
  Crypto::Session session( raw_key( master ) );
  const std::string sealed = session.encrypt( Crypto::Message( Crypto::Nonce( 0 ), id ) );
  return sealed.substr( 8, TICKET_KEY_LEN ); /* (after the nonce mosh puts first; before the tag) */
}

struct Ticket
{
  std::string id;
  std::string key;
};

/* A ticket under this master key: random8 is 8 bytes the caller drew at random. */
inline Ticket make_ticket( const std::string& master, const std::string& random8, uint64_t until )
{
  Ticket t;
  t.id = random8.substr( 0, 8 );
  for ( int shift = 56; shift >= 0; shift -= 8 ) {
    t.id.push_back( static_cast<char>( ( until >> shift ) & 0xff ) );
  }
  t.key = ticket_key( master, t.id );
  return t;
}

/* A ticket as text, to be printed and carried over ssh: "gosh-ticket ID KEY", each 22 letters of base64. */
inline std::string ticket_text( const Ticket& t )
{
  return "gosh-ticket " + raw_key( t.id ).printable_key() + " " + raw_key( t.key ).printable_key();
}

/* The other way: true, and the ticket, if the text is exactly that. */
inline bool parse_ticket( const std::string& text, Ticket& t )
{
  if ( text.size() != 12 + 22 + 1 + 22 || text.compare( 0, 12, "gosh-ticket " ) != 0 || text[34] != ' ' ) {
    return false;
  }
  try {
    Crypto::Base64Key id( text.substr( 12, 22 ) );
    Crypto::Base64Key key( text.substr( 35, 22 ) );
    t.id.assign( reinterpret_cast<const char*>( id.data() ), 16 );
    t.key.assign( reinterpret_cast<const char*>( key.data() ), 16 );
    return true;
  } catch ( const Crypto::CryptoException& ) {
    return false;
  }
}

}

#endif
