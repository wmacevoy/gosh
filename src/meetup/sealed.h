/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * Sealed text, and the text it opened to (REQ6/SHAPE-5). Not proved: it calls mosh's cipher.
 *
 * Sealed<T, Creds> is what a T was sealed as, under credentials of the kind Creds: each kind of
 * credentials says how it opens, and what it checks besides the cipher (messages.h: MeetupSealer).
 * Its unseal makes an Unsealed<T>, and nothing else can: the constructor takes a Key that only a
 * Sealed can make, so the compiler holds it.
 *
 * Unsealed<T> owns what the cipher gave back, made from it in place and never copied (the copy
 * constructor is deleted): a T reads it where it lies, as an Opened<T> (strong/opened.h), by read.
 * The owner: "for performance it is crucial that data is not copied just because."
 */

#ifndef GOSH_MEETUP_SEALED_H
#define GOSH_MEETUP_SEALED_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "src/crypto/crypto.h"
#include "src/strong/opened.h"

namespace Gosh {

template <typename T, typename Creds>
class Sealed;

template <typename T>
class Unsealed
{
public:
  /* What only a Sealed can make, and so the only way to an Unsealed. */
  class Key
  {
    Key() {}
    template <typename, typename>
    friend class Sealed;
  };

  /* The text sealed here, opened under this cipher; T's own text begins `begins` bytes in. Throws as decrypt does. */
  Unsealed( Key, Crypto::Session& session, const char* sealed, size_t n, size_t from )
    : message( session.decrypt( sealed, n ) ), begins( from ), counter_of( 0 )
  {}
  Unsealed( const Unsealed& ) = delete;
  Unsealed& operator=( const Unsealed& ) = delete;

  /* T reads its text, where it lies. */
  bool read( T& reader ) const
  {
    return reader.read_checked( Strong::Opened<T>( reinterpret_cast<const unsigned char*>( message.text.data() ) + begins,
                                                   message.text.size() - begins ) );
  }

  /* Whether T's text begins with these bytes, compared where it lies. */
  bool starts_with( const std::string& bytes ) const
  {
    return message.text.size() - begins >= bytes.size() && message.text.compare( begins, bytes.size(), bytes ) == 0;
  }

  /* The counter its nonce carried, as its Sealed read it. */
  uint64_t counter( void ) const { return counter_of; }

private:
  const Crypto::Message message;
  const size_t begins; /* at most message.text.size(): its Sealed keeps it no Unsealed otherwise */
  uint64_t counter_of;

  template <typename, typename>
  friend class Sealed;
};

}

#endif
