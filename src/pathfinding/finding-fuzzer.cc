/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-3, REQ15/PATH-4, REQ17/PATH-8: fuzzes path-finding's logic (finding.h) as the
 * Internet sees it: anyone can send a datagram to a relay's path-finding socket. One end, A, is
 * given datagrams of any kind from anyone (from the other end's address, from the meetup's, from
 * strangers, by IPv4 and IPv6), and real probes from a second end, B, sealed with the same key
 * (as they arrive, or with a byte changed, or from somewhere else), so that it gets past the
 * handshake as well as being thrown junk.
 *
 * Besides looking for crashes, it checks that A takes a datagram for the session's (DATA) only
 * from where its way is: the address it says it has found.
 *
 * New in gosh (queue item 21). The fork's punch fuzzer read STUN answers alone (stun-fuzzer.cc);
 * the fault it missed, an oversized probe that reached the cipher's assert (FINDINGS, 2026-11-15),
 * is in this one's corpus ("long-probe").
 *
 * Both ends use a meetup at MEETUP_AT with one ticket, and the harness plays it with the meetup's
 * own logic (registrations.h, answer.h): what an end sends there is answered or passed on as a
 * meetup would, so that a way through the meetup is found, and datagrams and goodbyes go by it.
 * (Until the review of item 21, 2026-11-16, B had no meetup and A's datagrams to it went to B, so
 * none of finding.h's meetup branches was reached.)
 *
 * Input: records of a kind byte, a length byte and that many bytes of body:
 *   0  a datagram to A: body[0] says from whom, body[1] how many bytes of padding it gets (x40)
 *   1  B's datagrams: to the meetup, played; to A, maybe one with a byte changed (body[0] which
 *      byte; body[1] & 1), maybe from a stranger's address (body[1] & 2), or lost (body[1] & 4);
 *      and first, B sends a datagram of the session (body[1] & 8) or says goodbye (body[1] & 16)
 *   2  A's datagrams: to the meetup, played; to B, or lost (body[0] & 4)
 *   3  time passes (body[0] x 50 ms), and both ends' timers run
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/meetup/answer.h"
#include "src/meetup/messages.h"
#include "src/meetup/registrations.h"
#include "src/meetup/ticket.h"
#include "src/pathfinding/finding.h"
#include "src/stun/stun.h"
#include "src/seam/fuzz-replay.h"

using namespace Network;

namespace {

void require( bool ok )
{
  if ( !ok ) {
    abort();
  }
}

Addr at( const char* ip_port )
{
  Addr a;
  require( parse_ip_port( ip_port, a ) );
  return a;
}

const char* const A_AT = "10.0.0.1:1000";
const char* const B_AT = "10.0.0.2:2000";
const char* const MEETUP_AT = "10.0.0.9:3478";
const char* const FROM[] = { B_AT, MEETUP_AT, "10.0.0.66:666", "[2001:db8::2]:2000", "[2001:db8::66]:666" };

/*
 * DATA only from where its way is: the other end's address, or the meetup's when the way is
 * through it. (Its bytes may be anything, even none: they are the session's to open, and it
 * refuses what it did not seal.)
 */
bool taken_from_its_way( const PathFinding& end, const Addr& from )
{
  return end.where() == ( end.direct() ? "" : "the meetup at " ) + format_ip_port( from );
}

uint8_t byte_at( const std::string& s, size_t i )
{
  return i < s.size() ? static_cast<uint8_t>( s[i] ) : 0;
}

const std::string MASTER = "0123456789abcdef";

struct KeyOf
{
  std::string operator()( const std::string& id ) const
  {
    try {
      return Gosh::ticket_key( MASTER, id );
    } catch ( ... ) {
      return std::string();
    }
  }
};

}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  const std::string key = "AAAAAAAAAAAAAAAAAAAAAA"; /* sixteen zero bytes, as Base64Key prints them */
  uint64_t now = 1000000;
  PathFinding a( true, key, now );
  PathFinding b( false, key, now );
  a.add_candidate( B_AT );
  a.add_candidate( "[2001:db8::2]:2000" );
  b.add_candidate( A_AT );
  const Gosh::Ticket ticket = Gosh::make_ticket( MASTER, "random!!", 4000000000ULL );
  a.use_meetup( MEETUP_AT, ticket, 1700000000000ULL );
  b.use_meetup( MEETUP_AT, ticket, 1700000000000ULL );
  Gosh::Registrations meetup( 0 ); /* (its clock, now / 1000, is far from the ends', which they learn by TIME: REQ18/PATH-5) */
  const Addr meetup_at = at( MEETUP_AT );

  /* A datagram an end sent to the meetup, from its address there: answered or passed on as the meetup would. */
  auto to_meetup = [&]( bool from_a, const std::string& bytes ) {
    const std::string from = Gosh::meetup_where_of( at( from_a ? A_AT : B_AT ) );
    std::string got, reply;
    Addr dest;
    socklen_t len;
    bool to_a;
    if ( Gosh::is_stun_request( bytes.data(), bytes.size() ) ) {
      reply = Gosh::stun_answer_to( bytes.data(), from );
      to_a = from_a;
    } else {
      const Gosh::Registrations::Send s = meetup.datagram( bytes.data(), bytes.size(), from, now / 1000, KeyOf() );
      if ( !s.any || !Gosh::meetup_addr_of( s.to, dest, len ) ) {
        return;
      }
      reply = s.bytes;
      to_a = same_ip_port( dest, at( A_AT ) );
      require( to_a || same_ip_port( dest, at( B_AT ) ) );
    }
    if ( to_a ) {
      if ( a.receive( now, reply.data(), reply.size(), meetup_at, got ) == PathFinding::DATA ) {
        require( taken_from_its_way( a, meetup_at ) );
      }
    } else {
      b.receive( now, reply.data(), reply.size(), meetup_at, got );
    }
  };

  size_t i = 0;
  while ( i + 2 <= size ) {
    const unsigned int kind = data[i] & 3;
    const size_t len = data[i + 1];
    const std::string body( reinterpret_cast<const char*>( data + i + 2 ), std::min( len, size - i - 2 ) );
    i += 2 + body.size();

    std::string datagram;
    switch ( kind ) {
      case 0: { /* a datagram from anyone */
        const Addr from = at( FROM[byte_at( body, 0 ) % 5] );
        std::string bytes = body.size() > 2 ? body.substr( 2 ) : std::string();
        bytes += std::string( byte_at( body, 1 ) * 40, 'p' );
        if ( a.receive( now, bytes.data(), bytes.size(), from, datagram ) == PathFinding::DATA ) {
          require( taken_from_its_way( a, from ) );
        }
        break;
      }
      case 1: { /* B's datagrams: to the meetup, or to A */
        if ( byte_at( body, 1 ) & 8 ) {
          b.send( "a datagram of the session" );
        }
        if ( byte_at( body, 1 ) & 16 ) {
          b.goodbye();
        }
        const std::vector<PathFinding::Outgoing> out = b.take_outbox();
        for ( size_t k = 0; k < out.size(); k++ ) {
          if ( same_ip_port( out[k].to, meetup_at ) ) {
            to_meetup( false, out[k].bytes );
            continue;
          }
          if ( byte_at( body, 1 ) & 4 ) {
            continue; /* lost on the way */
          }
          std::string bytes = out[k].bytes;
          if ( ( byte_at( body, 1 ) & 1 ) && !bytes.empty() ) {
            bytes[byte_at( body, 0 ) % bytes.size()] ^= 0x5A;
          }
          const Addr from = at( ( byte_at( body, 1 ) & 2 ) ? "10.0.0.66:666" : B_AT );
          if ( a.receive( now, bytes.data(), bytes.size(), from, datagram ) == PathFinding::DATA ) {
            require( taken_from_its_way( a, from ) );
          }
        }
        break;
      }
      case 2: { /* A's datagrams: to the meetup, or to B */
        const std::vector<PathFinding::Outgoing> out = a.take_outbox();
        const Addr from = at( A_AT );
        for ( size_t k = 0; k < out.size(); k++ ) {
          if ( same_ip_port( out[k].to, meetup_at ) ) {
            to_meetup( true, out[k].bytes );
          } else if ( !( byte_at( body, 0 ) & 4 ) ) {
            b.receive( now, out[k].bytes.data(), out[k].bytes.size(), from, datagram );
          }
        }
        break;
      }
      case 3: /* time passes */
        now += byte_at( body, 0 ) * 50;
        a.tick( now );
        b.tick( now );
        break;
    }
  }
  return 0;
}
