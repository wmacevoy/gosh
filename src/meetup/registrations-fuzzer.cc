/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ15/PATH-5: fuzzes the meetup's logic as the Internet sees it (registrations.h, answer.h): a
 * meetup's port is open to anyone. The meetup is given datagrams of any kind from any of a few
 * addresses (IPv4 and IPv6); messages sealed under the keys of two good tickets, by either end,
 * from where the sender says it is or from somewhere else, with any counter; earlier messages
 * again, from anywhere; and STUN requests.
 *
 * Besides looking for crashes, it holds the meetup to PATH-5, keeping a model of what the
 * meetup's own answers have shown:
 *   - an end is registered at an address once the meetup has answered its REGISTER there;
 *   - REGISTERED goes only to where the REGISTER came from, and only if it was sealed there;
 *   - PASSED goes only from an end's registered address, only to the other end of the same
 *     ticket at its registered address, and carries what was sealed;
 *   - nothing at all is sent for bytes that were not sealed under a ticket's key (junk); and a
 *     message sent again, from anywhere, is held to the same rules as when it was first sent: it
 *     can do no more than its sender could;
 *   - an answer to STUN is a whole answer, of the length for its family.
 *
 * New in gosh (queue item 21); the fork's meetup was fuzzed by nothing.
 *
 * Input: records of a kind byte, a length byte and that many bytes of body:
 *   0  bytes from anyone (body[0] says from where; the rest are the datagram), or, if body[0] has
 *      its top bit, the last sealed message again, from there
 *   1  a REGISTER: body[0] ticket and end, body[1] from where, body[2] a step for its counter,
 *      body[3] where it says it is
 *   2  a PASS: body[0] ticket and end, body[1] from where, body[2] a step for its counter; the
 *      rest is the datagram
 *   3  time passes (body[0] seconds), and a STUN request from body[1]'s address
 */

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

#include "src/meetup/answer.h"
#include "src/meetup/messages.h"
#include "src/meetup/registrations.h"
#include "src/meetup/ticket.h"
#include "src/seam/fuzz-replay.h"

using namespace Gosh;

namespace {

void require( bool ok )
{
  if ( !ok ) {
    abort();
  }
}

const std::string MASTER = "0123456789abcdef";
const uint64_t UNTIL = 4000000000ULL;

std::string where( unsigned int which )
{
  switch ( which % 5 ) {
    case 0:
      return meetup_where_v4( 0x0A000001, 1000 );
    case 1:
      return meetup_where_v4( 0x0A000002, 2000 );
    case 2:
      return meetup_where_v4( 0x0A000042, 666 );
    case 3: {
      std::string w = meetup_where_v4( 0, 3000 );
      w[0] = 6;
      w[1] = 0x20;
      w[2] = 0x01;
      w[16] = 0x03;
      return w;
    }
    default: {
      std::string w = meetup_where_v4( 0, 4000 );
      w[0] = 6;
      w[1] = 0x20;
      w[2] = 0x01;
      w[16] = 0x04;
      return w;
    }
  }
}

struct KeyOf
{
  std::string operator()( const std::string& id ) const
  {
    try {
      return ticket_key( MASTER, id );
    } catch ( ... ) {
      return std::string();
    }
  }
};

/* A message sealed by an end, as sent: its kind, ticket and end, what it says, and its bytes. */
struct Sealed
{
  MeetupKind kind;
  int ticket;
  int end;
  std::string text;
  std::string bytes;
  Sealed() : kind( MEETUP_REGISTER ), ticket( 0 ), end( 0 ), text(), bytes() {}
};

uint8_t byte_at( const std::string& s, size_t i )
{
  return i < s.size() ? static_cast<uint8_t>( s[i] ) : 0;
}

}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  const Ticket tickets[2] = { make_ticket( MASTER, "ticket-1", UNTIL ), make_ticket( MASTER, "ticket-2", UNTIL ) };
  Crypto::Session session0( raw_key( tickets[0].key ) );
  Crypto::Session session1( raw_key( tickets[1].key ) );
  Crypto::Session* sessions[2] = { &session0, &session1 };

  Registrations meetup;
  AnswerRate rate;
  const KeyOf key_of = KeyOf();
  uint64_t now = 1000000;
  uint64_t counters[2][2] = { { 1000, 1000 }, { 1000, 1000 } };
  std::map<int, std::string> registered; /* ticket * 2 + end: where the meetup's answer showed it registered */
  Sealed last;                           /* the last message sealed, to be sent again */

  /* What the meetup sent, if anything, for this sealed message from this address: held to PATH-5. */
  auto held = [&]( const Sealed& m, const std::string& from, const Registrations::Send& s ) {
    if ( !s.any ) {
      return;
    }
    const int me = m.ticket * 2 + m.end, other = m.ticket * 2 + 1 - m.end;
    if ( m.kind == MEETUP_REGISTER ) {
      /* answered only where it came from, and only if it was sealed there */
      require( s.to == from && m.text == from );
      MeetupHeader h;
      uint64_t counter;
      std::string seen;
      require( meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && h.kind == MEETUP_REGISTERED
               && meetup_open( *sessions[m.ticket], h, true, counter, seen ) && seen == from );
      registered[me] = from;
    } else {
      /* only from where it registered, only to the other end of its ticket where that registered */
      require( registered.count( me ) && registered[me] == from );
      require( registered.count( other ) && s.to == registered[other] );
      require( s.bytes == meetup_passed( m.text ) && !m.text.empty() );
    }
  };

  size_t i = 0;
  while ( i + 2 <= size ) {
    const unsigned int kind = data[i] & 3;
    const size_t len = data[i + 1];
    const std::string body( reinterpret_cast<const char*>( data + i + 2 ), len < size - i - 2 ? len : size - i - 2 );
    i += 2 + body.size();

    switch ( kind ) {
      case 0: { /* anything at all: nothing is ever sent for it; or a message again, held to the rules */
        const std::string from = where( byte_at( body, 0 ) & 0x7F );
        if ( byte_at( body, 0 ) & 0x80 ) {
          held( last, from, meetup.datagram( last.bytes.data(), last.bytes.size(), from, now, key_of ) );
        } else {
          const std::string bytes = body.substr( body.empty() ? 0 : 1 );
          require( !meetup.datagram( bytes.data(), bytes.size(), from, now, key_of ).any );
        }
        break;
      }

      case 1:   /* a REGISTER */
      case 2: { /* a PASS */
        Sealed m;
        m.kind = kind == 1 ? MEETUP_REGISTER : MEETUP_PASS;
        m.ticket = byte_at( body, 0 ) & 1;
        m.end = ( byte_at( body, 0 ) >> 1 ) & 1;
        const std::string from = where( byte_at( body, 1 ) );
        counters[m.ticket][m.end] += static_cast<int8_t>( byte_at( body, 2 ) );
        m.text = kind == 1 ? where( byte_at( body, 3 ) ) : ( body.size() > 3 ? body.substr( 3 ) : std::string() );
        m.bytes = meetup_seal( *sessions[m.ticket], m.kind, tickets[m.ticket].id, m.end, false, counters[m.ticket][m.end], m.text );
        last = m;
        held( m, from, meetup.datagram( m.bytes.data(), m.bytes.size(), from, now, key_of ) );
        break;
      }

      case 3: { /* time passes, and someone asks where they are */
        now += byte_at( body, 0 );
        const std::string from = where( byte_at( body, 1 ) );
        const std::string request = body.size() > 2 ? body.substr( 2 ) : std::string();
        if ( is_stun_request( request.data(), request.size() ) && rate.allow( now ) ) {
          const std::string answer = stun_answer_to( request.data(), from );
          require( answer.size() == ( from[0] == 4 ? 32u : 44u ) );
        }
        break;
      }
    }
    require( meetup.ticket_count() <= 2 );
  }
  return 0;
}
