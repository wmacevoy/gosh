/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The text of the functions is the fork's (mosh-1.4.0-pf.7, src/network/relay.cc and relay.h). */

/*
 * relay, frame: datagrams on the ssh stream, and the words the two relays say to each other
 * (REQ1/PATH-1; design v2, 2.2). Logic: bytes in, bytes out.
 *
 * On the stream each datagram is preceded by its length, as two bytes, most significant first.
 * A frame that begins with the relays' mark is not a datagram of the session's but words for
 * the other relay, separated by spaces.
 *
 * Absent or late bytes: a frame is given out only when all of it has arrived, however the
 * stream was cut up on the way. Wrong: a datagram longer than 65,535 bytes cannot be framed
 * (its length would not fit); the relays never read one longer. Lying: a length is believed;
 * the stream is ssh's, from the other relay, and what comes out of a frame is a sealed
 * datagram that the session will refuse if it is not its own.
 */

#ifndef GOSH_RELAY_FRAME_H
#define GOSH_RELAY_FRAME_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Gosh::Relay::Imp {

/* A datagram as it goes on the stream. */
inline std::string relay_frame( const std::string& datagram )
{
  std::string ret;
  ret.reserve( 2 + datagram.size() );
  ret.push_back( static_cast<char>( datagram.size() >> 8 ) );
  ret.push_back( static_cast<char>( datagram.size() & 0xFF ) );
  ret += datagram;
  return ret;
}

/* Takes the stream apart again. */
class RelayFrameParser
{
private:
  std::string pending;

public:
  RelayFrameParser() : pending() {}

  void append( const char* data, size_t len ) { pending.append( data, len ); }

  /* The next whole datagram, if one has arrived. */
  bool next( std::string& datagram )
  {
    if ( pending.size() < 2 ) {
      return false;
    }
    const size_t len = ( static_cast<uint8_t>( pending[0] ) << 8 ) | static_cast<uint8_t>( pending[1] );
    if ( pending.size() < 2 + len ) {
      return false;
    }
    datagram.assign( pending, 2, len );
    pending.erase( 0, 2 + len );
    return true;
  }
};

/* Words for the other relay, as a frame: the mark, then the text. */
inline std::string relay_words( const std::string& mark, const std::string& text )
{
  return relay_frame( mark + text );
}

/* Whether what came out of a frame is words for us (it begins with the mark); and if so, the words. */
inline bool relay_words_of( const std::string& mark, const std::string& frame, std::vector<std::string>& words )
{
  if ( frame.size() < mark.size() || memcmp( frame.data(), mark.data(), mark.size() ) != 0 ) {
    return false;
  }
  words.clear();
  std::string word;
  for ( size_t i = mark.size(); i <= frame.size(); i++ ) {
    if ( i == frame.size() || frame[i] == ' ' ) {
      if ( !word.empty() ) {
        words.push_back( word );
      }
      word.clear();
    } else {
      word.push_back( frame[i] );
    }
  }
  return true;
}

}

#endif
