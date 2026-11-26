/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* forwarding, TCP: the byte queue a connection's two buffers are (REQ14/SPEED-1). Logic. */

#ifndef GOSH_TCP_BYTES_H
#define GOSH_TCP_BYTES_H

#include <cstddef>
#include <string>

namespace Gosh::Forwarding::Imp {

/*
 * Bytes waiting in order: added at the end, taken from the front.
 *
 * A connection's two buffers are these. Taking from the front of a std::string moves everything
 * left in it down, and the sender did that with up to 1 MB on every acknowledgment: about 40% of
 * all the processor time of a forward, by a profile of the fork (docs/FINDINGS.md, 2026-11-14).
 * Here the front is only marked as gone, and what is left is moved down once the gone part is
 * at least 64 KB and at least half of what is left: so no byte is moved more than twice for
 * each byte taken, and no more than half as much again as the bytes waiting is ever kept.
 */
class Bytes
{
  std::string bytes;
  size_t gone; /* how much at the front of `bytes` has been taken */

public:
  static const size_t TIDY_AT = 65536;

  Bytes() : bytes(), gone( 0 ) {}

  size_t size( void ) const { return bytes.size() - gone; }
  bool empty( void ) const { return size() == 0; }
  const char* data( void ) const { return bytes.data() + gone; }

  void append( const char* more, size_t length ) { bytes.append( more, length ); }
  /* length bytes of `from`, starting at `at`. */
  void append( const std::string& from, size_t at, size_t length ) { bytes.append( from, at, length ); }

  /* Up to length bytes starting `at` bytes in, as a string. */
  std::string substr( size_t at, size_t length ) const { return bytes.substr( gone + at, length ); }

  /* Takes this many from the front (all of them, if there are fewer). */
  void drop( size_t length )
  {
    gone += std::min( length, size() );
    if ( gone == bytes.size() ) {
      bytes.clear();
      gone = 0;
    } else if ( gone >= TIDY_AT && gone >= ( bytes.size() - gone ) / 2 ) {
      bytes.erase( 0, gone );
      gone = 0;
    }
  }
};

}

#endif
