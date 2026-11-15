/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The first test is the fork's (mosh-1.4.0-pf.7, src/tests/udp-forward.cc, test_relay_frames). */

/* REQ1/PATH-1: datagrams on the ssh stream, and the relays' words to each other (frame.h). */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/relay/frame.h"

using Gosh::relay_frame;
using Gosh::RelayFrameParser;

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-88s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static std::string varied( size_t length )
{
  std::string s( length, 0 );
  for ( size_t i = 0; i < length; i++ ) {
    s[i] = static_cast<char>( ( i * 2654435761u ) >> 9 );
  }
  return s;
}

static void test_relay_frames( void )
{
  const std::string a = "hello", b = "", c = varied( 65535 );
  const std::string stream = relay_frame( a ) + relay_frame( b ) + relay_frame( c );
  check( "three datagrams of 5, 0 and 65,535 bytes: each with two bytes before it", stream.size() == 5 + 0 + 65535 + 3 * 2 );
  check( "the two bytes are the length, most significant first", stream.substr( 0, 2 ) == std::string( "\x00\x05", 2 ) );

  /* however the stream is cut up on the way, the same datagrams come out */
  bool same = true, none_left = true;
  for ( size_t step = 1; step < 70000; step = step * 7 + 1 ) {
    RelayFrameParser parser;
    std::vector<std::string> out;
    std::string datagram;
    for ( size_t at = 0; at < stream.size(); at += step ) {
      parser.append( stream.data() + at, std::min( step, stream.size() - at ) );
      while ( parser.next( datagram ) ) {
        out.push_back( datagram );
      }
    }
    same = same && out.size() == 3 && out[0] == a && out[1] == b && out[2] == c;
    none_left = none_left && !parser.next( datagram );
  }
  check( "however the stream is cut up on the way (1 byte at a time to all at once), the same three come out", same );
  check( "and nothing after them", none_left );

  RelayFrameParser part;
  std::string datagram;
  part.append( stream.data(), 6 ); /* the length and four of five bytes */
  check( "a datagram not all there yet is not given out", !part.next( datagram ) );
  part.append( stream.data() + 6, 1 );
  check( "and is, when its last byte arrives", part.next( datagram ) && datagram == a );
}

static void test_words( void )
{
  const std::string mark( "\xff\xfe" "relay!", 8 );
  const std::string frame = Gosh::relay_words( mark, "PORT 40123" );
  check( "words for the other relay are a frame: the length, the mark, the text", frame == relay_frame( mark + "PORT 40123" ) );

  RelayFrameParser parser;
  parser.append( frame.data(), frame.size() );
  std::string inside;
  std::vector<std::string> words;
  check( "taken apart, it is known for words, and they are read",
         parser.next( inside ) && Gosh::relay_words_of( mark, inside, words ) && words.size() == 2 && words[0] == "PORT"
           && words[1] == "40123" );
  check( "several spaces are one; the mark alone is no words",
         Gosh::relay_words_of( mark, mark + "  A   B ", words ) && words.size() == 2 && words[1] == "B"
           && Gosh::relay_words_of( mark, mark, words ) && words.empty() );
  words.assign( 1, "kept" );
  check( "a datagram of the session's is not words, and what the caller had is left alone",
         !Gosh::relay_words_of( mark, "a sealed datagram", words ) && words.size() == 1 );
  check( "nor is one shorter than the mark, nor one that only begins like it",
         !Gosh::relay_words_of( mark, mark.substr( 0, 7 ), words ) && !Gosh::relay_words_of( mark, mark.substr( 0, 7 ) + "?PORT 1", words ) );
}

int main()
{
  test_relay_frames();
  test_words();

  if ( failures ) {
    printf( "FAIL REQ1/PATH-1: %d checks of the relay's frames failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/PATH-1: the relay's frames and words\n" );
  return EXIT_SUCCESS;
}
