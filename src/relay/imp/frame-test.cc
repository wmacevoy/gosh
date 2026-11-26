/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The first test is the fork's (mosh-1.4.0-pf.7, src/tests/udp-forward.cc, test_relay_frames). */

/* REQ1/PATH-1: datagrams on the ssh stream, and the relays' words to each other (frame.h). */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/relay/imp/frame.h"
#include "vendor/facts/facts.h"

using Gosh::Relay::Imp::relay_frame;
using Gosh::Relay::Imp::RelayFrameParser;



static std::string varied( size_t length )
{
  std::string s( length, 0 );
  for ( size_t i = 0; i < length; i++ ) {
    s[i] = static_cast<char>( ( i * 2654435761u ) >> 9 );
  }
  return s;
}
FACTS( RelayFrames )
{
  const std::string a = "hello", b = "", c = varied( 65535 );
  const std::string stream = relay_frame( a ) + relay_frame( b ) + relay_frame( c );
  FACT_CHECK( stream.size() == 5 + 0 + 65535 + 3 * 2, "three datagrams of 5, 0 and 65,535 bytes: each with two bytes before it" );
  FACT_CHECK( stream.substr( 0, 2 ) == std::string( "\x00\x05", 2 ), "the two bytes are the length, most significant first" );

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
  FACT_CHECK( same, "however the stream is cut up on the way (1 byte at a time to all at once), the same three come out" );
  FACT_CHECK( none_left, "and nothing after them" );

  RelayFrameParser part;
  std::string datagram;
  part.append( stream.data(), 6 ); /* the length and four of five bytes */
  FACT_CHECK( !part.next( datagram ), "a datagram not all there yet is not given out" );
  part.append( stream.data() + 6, 1 );
  FACT_CHECK( ( part.next( datagram ) && datagram == a ), "and is, when its last byte arrives" );
}
FACTS( Words )
{
  const std::string mark( "\xff\xfe" "relay!", 8 );
  const std::string frame = Gosh::Relay::Imp::relay_words( mark, "PORT 40123" );
  FACT_CHECK( frame == relay_frame( mark + "PORT 40123" ), "words for the other relay are a frame: the length, the mark, the text" );

  RelayFrameParser parser;
  parser.append( frame.data(), frame.size() );
  std::string inside;
  std::vector<std::string> words;
  FACT_CHECK( ( parser.next( inside ) && Gosh::Relay::Imp::relay_words_of( mark, inside, words ) && words.size() == 2 && words[0] == "PORT"
           && words[1] == "40123" ), "taken apart, it is known for words, and they are read" );
  FACT_CHECK( ( Gosh::Relay::Imp::relay_words_of( mark, mark + "  A   B ", words ) && words.size() == 2 && words[1] == "B"
           && Gosh::Relay::Imp::relay_words_of( mark, mark, words ) && words.empty() ), "several spaces are one; the mark alone is no words" );
  words.assign( 1, "kept" );
  FACT_CHECK( ( !Gosh::Relay::Imp::relay_words_of( mark, "a sealed datagram", words ) && words.size() == 1 ), "a datagram of the session's is not words, and what the caller had is left alone" );
  FACT_CHECK( ( !Gosh::Relay::Imp::relay_words_of( mark, mark.substr( 0, 7 ), words ) && !Gosh::Relay::Imp::relay_words_of( mark, mark.substr( 0, 7 ) + "?PORT 1", words ) ), "nor is one shorter than the mark, nor one that only begins like it" );
}
/* REQ1/PATH-1: the relay's frames and words */
FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( RelayFrames );
  FACTS_REGISTER( Words );
}

FACTS_MAIN
