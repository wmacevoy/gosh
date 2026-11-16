/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-3, REQ16/PATH-8: fuzzes what path-finding has to make sense of from anyone at all on
 * the Internet (stun.h): answers from a STUN server (or from whoever says they are one), IPv4 and
 * IPv6; and addresses given as text.
 *
 * (gosh) The fork's src/fuzz/punch_fuzzer.cc (mosh-1.4.0-pf.7), against stun.h, which is where
 * those functions are now. Its corpus is the fork's.
 */

#include <cstdint>
#include <cstdlib>
#include <string>

#include "src/pathfinding/stun.h"
#include "src/seam/fuzz-replay.h"

using namespace Network;

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  /* an exact copy, so that reading past the end is seen */
  char* copy = static_cast<char*>( malloc( size ? size : 1 ) );
  if ( !copy ) {
    return 0;
  }
  for ( size_t i = 0; i < size; i++ ) {
    copy[i] = static_cast<char>( data[i] );
  }

  Addr mapped;
  if ( stun_parse( copy, size, mapped ) ) {
    /* whatever it said, it is an address that can be written down and read back */
    Addr again;
    const std::string text = format_ip_port( mapped );
    if ( text.empty() ) {
      abort();
    }
    if ( mapped.sin.sin_port != 0 && ( !parse_ip_port( text, again ) || format_ip_port( again ) != text ) ) {
      abort();
    }
  }

  Addr addr;
  const std::string text( copy, size );
  if ( parse_ip_port( text, addr ) && format_ip_port( addr ).empty() ) {
    abort();
  }
  free( copy );
  return 0;
}
