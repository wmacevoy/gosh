/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-1, REQ15/PATH-4: fuzzes what a relay takes from the ssh stream (frame.h): the stream
 * taken apart into datagrams, however it was cut up on the way; the words of the other relay
 * among them; and a ticket among the words (ticket.h's parse_ticket). The other relay is trusted
 * no more than the ssh connection is, and what it says is read by code that must not be led
 * astray by its lengths or its text.
 *
 * Held to: the datagrams that come out, framed again, are exactly the stream as far as it was
 * read, and what is left is less than one whole frame; a frame's words are what is between its
 * spaces, none empty; a ticket read from text, written as text and read again, is the same ticket.
 *
 * New in gosh (the review of item 21, 2026-11-16: the relays' words had no fuzzer).
 *
 * Input: the first byte seeds how the rest is cut up as it arrives; the rest is the stream.
 */

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/meetup/ticket.h"
#include "src/pathfinding/mark.h"
#include "src/relay/frame.h"
#include "src/seam/fuzz-replay.h"

namespace {

void require( bool ok )
{
  if ( !ok ) {
    abort();
  }
}

/* A ticket as text is read back as the same ticket. */
void ticket_holds( const std::string& text )
{
  Gosh::Meetup::Ticket t, again;
  if ( Gosh::Meetup::parse_ticket( text, t ) ) {
    require( t.id.size() == 16 && t.key.size() == 16 );
    require( Gosh::Meetup::parse_ticket( Gosh::Meetup::ticket_text( t ), again ) && again.id == t.id && again.key == t.key );
  }
}

}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  if ( size < 1 ) {
    return 0;
  }
  const std::string mark( Network::PUNCH_MARK, sizeof( Network::PUNCH_MARK ) );
  const std::string stream( reinterpret_cast<const char*>( data + 1 ), size - 1 );
  uint32_t cut = data[0] | 1u;

  Gosh::RelayFrameParser parser;
  std::string again; /* the datagrams that came out, framed again */
  size_t given = 0;
  while ( given < stream.size() ) {
    cut = cut * 1103515245u + 12345u;
    const size_t piece = std::min<size_t>( 1 + ( cut >> 16 ) % 97, stream.size() - given );
    parser.append( stream.data() + given, piece );
    given += piece;
    std::string datagram;
    while ( parser.next( datagram ) ) {
      again += Gosh::relay_frame( datagram );
      std::vector<std::string> words;
      if ( Gosh::relay_words_of( mark, datagram, words ) ) {
        for ( size_t i = 0; i < words.size(); i++ ) {
          require( !words[i].empty() && words[i].find( ' ' ) == std::string::npos );
        }
        if ( words.size() == 3 && words[0] == "TICKET" ) {
          ticket_holds( "gosh-ticket " + words[1] + " " + words[2] );
        }
      }
    }
  }
  /* what came out is the stream, framed; and what is left is not yet a whole frame */
  require( stream.compare( 0, again.size(), again ) == 0 );
  const std::string left = stream.substr( again.size() );
  require( left.size() < 2 || left.size() < 2 + ( ( static_cast<uint8_t>( left[0] ) << 8 ) | static_cast<uint8_t>( left[1] ) ) );

  ticket_holds( stream );
  return 0;
}
