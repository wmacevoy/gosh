/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * copy, checksum: 128 bits over a file's bytes, to say whether what arrived is what was sent, and
 * whether a part already held is the start of the file being offered (REQ1/CP-2, REQ1/CP-3).
 * Not cryptographic: the session already is. Logic. The fork's (mosh-1.4.0-pf.7,
 * src/network/filecopy.cc, Hash), byte for byte: both ends must agree with a pf.7 end.
 */

#ifndef GOSH_COPY_CHECKSUM_H
#define GOSH_COPY_CHECKSUM_H

#include <cstddef>
#include <cstdint>

namespace Gosh::Copy::Imp {

struct Hash
{
  uint64_t a, b;
  Hash() : a( 0xcbf29ce484222325ULL ), b( 0x9e3779b97f4a7c15ULL ) {}

  /* Adding in pieces gives what adding the whole at once does. */
  void add( const char* data, size_t len )
  {
    uint64_t x = a, y = b;
    for ( size_t i = 0; i < len; i++ ) {
      const uint8_t c = static_cast<uint8_t>( data[i] );
      x = ( x ^ c ) * 0x100000001b3ULL; /* FNV-1a */
      y = ( ( y << 5 ) | ( y >> 59 ) ) ^ c;
      y *= 0xff51afd7ed558ccdULL;
    }
    a = x;
    b = y;
  }

  bool operator==( const Hash& x ) const { return a == x.a && b == x.b; }
};

}

#endif
