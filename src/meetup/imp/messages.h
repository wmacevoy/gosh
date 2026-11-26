/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, messages: the datagrams between an end and a meetup, as bytes (REQ18/PATH-5; the
 * proposal, revision 10, section 4). Logic: bytes and a cipher in, bytes out; no socket, no clock.
 *
 *   REGISTER    mark | version | ticket id (16) | end (1) | sealed { kind, where the end believes it is,
 *                                                                     the time by the meetup's clock (8) }   78 bytes
 *   REGISTERED  mark | version | ticket id (16) | end (1) | sealed { kind, where the end was seen }        70 bytes
 *   TIME        mark | version | ticket id (16) | end (1) | sealed { kind, the counter of the REGISTER
 *                                                                     it answers (8), the meetup's time (8) } 67 bytes
 *   PASS        mark | version | ticket id (16) | end (1) | sealed { kind, the datagram }      52 to 1,551 bytes
 *   PASSED      mark | version | the datagram                                                  10 to 1,509 bytes
 *
 * "sealed" is mosh's cipher under the ticket's key: an 8-byte nonce, the text, a 16-byte tag. The
 * nonce is a counter whose top two bits say who sealed it and for whom (strong/meetup/nonce.h), so that
 * no two of the four senders under one key ever use the same nonce, and a message sealed one way
 * cannot be taken for one sealed another. "where" is 19 bytes: a family (4 or 6), an address of
 * 16 (an IPv4 address in the first four, most significant first, zeros after; or an IPv6 address)
 * and a port of 2 (REQ17/PATH-8). A time is milliseconds since 1970, UTC, most significant byte first.
 *
 * Version 2 (2026-11-17, REQ18/PATH-5): REGISTER carries a time, so that a meetup that has let a
 * ticket go cannot be given an old REGISTER again; and a meetup whose clock the time is too far
 * from answers TIME, saying its own, so that an end need not have a right clock to register.
 *
 * Wrong input: anything not exactly one of these lengths, with its mark and version, is not read
 * (false); a seal that does not open, or opens to the wrong kind or direction, is refused. Lengths
 * are checked before the cipher is called: it guards its buffers with an assert alone.
 */

#ifndef GOSH_MEETUP_MESSAGES_H
#define GOSH_MEETUP_MESSAGES_H

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "src/strong/meetup/counter.h"
#include "src/strong/meetup/header.h"
#include "src/strong/meetup/nonce.h"
#include "src/strong/meetup/pass.h"
#include "src/strong/meetup/register.h"
#include "src/strong/meetup/registered.h"
#include "src/strong/meetup/time.h"
#include "src/crypto/crypto.h"
#include "src/meetup/imp/sealed.h"
#include "src/network/network.h"

namespace Gosh::Meetup::Imp {

enum MeetupKind
{
  MEETUP_REGISTER = 1,
  MEETUP_REGISTERED = 2,
  MEETUP_PASS = 3,
  MEETUP_PASSED = 4,
  MEETUP_TIME = 5
};

static const int MEETUP_KINDS = 5;
static constexpr char MEETUP_MARKS[MEETUP_KINDS][8] = { { 0, 'g', 'o', 's', 'h', 'R', 'E', 'G' },
                                                    { 0, 'g', 'o', 's', 'h', 'R', 'G', 'D' },
                                                    { 0, 'g', 'o', 's', 'h', 'P', 'A', 'S' },
                                                    { 0, 'g', 'o', 's', 'h', 'P', 'S', 'D' },
                                                    { 0, 'g', 'o', 's', 'h', 'T', 'I', 'M' } };
static const unsigned char MEETUP_VERSION = 2;

static const size_t MEETUP_WHERE_LEN = 19;
static const size_t MEETUP_HEADER = 8 + 1 + 16 + 1; /* mark, version, ticket id, end */
static const size_t MEETUP_SEAL = 8 + 16;           /* the nonce, the tag */
static const size_t MEETUP_ADDRESS_MESSAGE = MEETUP_HEADER + MEETUP_SEAL + 1 + MEETUP_WHERE_LEN; /* 70: REGISTERED */
static const size_t MEETUP_REGISTER_MESSAGE = MEETUP_ADDRESS_MESSAGE + 8;                        /* 78 */
static const size_t MEETUP_TIME_MESSAGE = MEETUP_HEADER + MEETUP_SEAL + 1 + 8 + 8;               /* 67 */
static const size_t MEETUP_MAX_DATAGRAM = 1500;
static const size_t MEETUP_PASS_MIN = MEETUP_HEADER + MEETUP_SEAL + 1 + 1;
static const size_t MEETUP_PASS_MAX = MEETUP_HEADER + MEETUP_SEAL + 1 + MEETUP_MAX_DATAGRAM;
static const uint64_t MEETUP_COUNTER_MASK = Strong::Meetup::COUNTER_MOST; /* 2^62 - 1 (strong/meetup/counter.h) */

/* Who sealed a message, and for whom, is the top two bits of its nonce: Strong::Meetup::Nonce
   (strong/meetup/nonce.h) says so, and is proved to lose nothing. */

/* An IPv4 address and port (each in host order) as "where". */
inline std::string meetup_where_v4( uint32_t address, uint16_t port )
{
  std::string w( MEETUP_WHERE_LEN, '\0' );
  w[0] = 4;
  for ( int i = 0; i < 4; i++ ) {
    w[1 + i] = static_cast<char>( ( address >> ( 24 - 8 * i ) ) & 0xff );
  }
  w[17] = static_cast<char>( port >> 8 );
  w[18] = static_cast<char>( port & 0xff );
  return w;
}

/* An IPv6 address (16 bytes, as on the wire) and port (in host order) as "where". */
inline std::string meetup_where_v6( const void* address, uint16_t port )
{
  std::string w( MEETUP_WHERE_LEN, '\0' );
  w[0] = 6;
  memcpy( &w[1], address, 16 );
  w[17] = static_cast<char>( port >> 8 );
  w[18] = static_cast<char>( port & 0xff );
  return w;
}

/* An address as "where": IPv4, or IPv6; an IPv6 address that is an IPv4 one mapped is taken as IPv4. Empty for anything else. */
inline std::string meetup_where_of( const Network::Addr& a )
{
  if ( a.sa.sa_family == AF_INET ) {
    return meetup_where_v4( ntohl( a.sin.sin_addr.s_addr ), ntohs( a.sin.sin_port ) );
  }
  if ( a.sa.sa_family == AF_INET6 ) {
    const unsigned char* b = a.sin6.sin6_addr.s6_addr;
    static const unsigned char mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    if ( memcmp( b, mapped, 12 ) == 0 ) {
      return meetup_where_v4( ( uint32_t( b[12] ) << 24 ) | ( b[13] << 16 ) | ( b[14] << 8 ) | b[15], ntohs( a.sin6.sin6_port ) );
    }
    return meetup_where_v6( b, ntohs( a.sin6.sin6_port ) );
  }
  return std::string();
}

/* "where" back to an address to send to, and its length; false if it is not one. */
inline bool meetup_addr_of( const std::string& w, Network::Addr& a, socklen_t& len )
{
  if ( w.size() != MEETUP_WHERE_LEN || ( w[0] != 4 && w[0] != 6 ) ) {
    return false;
  }
  memset( &a, 0, sizeof( a ) );
  if ( w[0] == 4 ) {
    a.sin.sin_family = AF_INET;
    memcpy( &a.sin.sin_addr.s_addr, w.data() + 1, 4 );
    memcpy( &a.sin.sin_port, w.data() + 17, 2 );
    len = sizeof( a.sin );
  } else {
    a.sin6.sin6_family = AF_INET6;
    memcpy( a.sin6.sin6_addr.s6_addr, w.data() + 1, 16 );
    memcpy( &a.sin6.sin6_port, w.data() + 17, 2 );
    len = sizeof( a.sin6 );
  }
  return true;
}

/* Eight bytes, most significant first, and back. */
inline std::string meetup_u64( uint64_t v )
{
  std::string s( 8, '\0' );
  for ( int i = 0; i < 8; i++ ) {
    s[i] = static_cast<char>( ( v >> ( 56 - 8 * i ) ) & 0xff );
  }
  return s;
}
inline uint64_t meetup_u64_of( const char* p )
{
  uint64_t v = 0;
  for ( int i = 0; i < 8; i++ ) {
    v = ( v << 8 ) | static_cast<unsigned char>( p[i] );
  }
  return v;
}

/* REGISTER's text: where the end believes it is, and the time (ms) by the meetup's clock, as the end reckons it. */
inline std::string meetup_register_text( const std::string& where, uint64_t time_ms )
{
  return where + meetup_u64( time_ms );
}

/* TIME's text: the counter of the REGISTER it answers, and the meetup's time (ms). */
inline std::string meetup_time_text( uint64_t answering, uint64_t time_ms )
{
  return meetup_u64( answering ) + meetup_u64( time_ms );
}

/* A message's header, once its length, mark and version have been found right. */
struct MeetupHeader
{
  MeetupKind kind;
  std::string ticket;  /* 16 bytes; empty for PASSED */
  int end;             /* 0 or 1 */
  const char* sealed;  /* the sealed part, and its length; for PASSED, the datagram */
  size_t sealed_len;
};

/* What strong/meetup/header.h writes out as numbers are these: the compiler is asked. */
static_assert( MEETUP_VERSION == 2 && MEETUP_HEADER == 26 && MEETUP_REGISTER_MESSAGE == 78 && MEETUP_ADDRESS_MESSAGE == 70
                 && MEETUP_TIME_MESSAGE == 67 && MEETUP_PASS_MIN == 52 && MEETUP_PASS_MAX == 1551 && MEETUP_MAX_DATAGRAM == 1500,
               "strong/meetup/header.h says other lengths than these" );
static_assert( MEETUP_MARKS[0][0] == 0 && MEETUP_MARKS[0][1] == 0x67 && MEETUP_MARKS[0][2] == 0x6f && MEETUP_MARKS[0][3] == 0x73
                 && MEETUP_MARKS[0][4] == 0x68 && MEETUP_MARKS[0][5] == 0x52 && MEETUP_MARKS[0][6] == 0x45 && MEETUP_MARKS[0][7] == 0x47
                 && MEETUP_MARKS[1][5] == 0x52 && MEETUP_MARKS[1][6] == 0x47 && MEETUP_MARKS[1][7] == 0x44 && MEETUP_MARKS[2][5] == 0x50
                 && MEETUP_MARKS[2][6] == 0x41 && MEETUP_MARKS[2][7] == 0x53 && MEETUP_MARKS[3][5] == 0x50 && MEETUP_MARKS[3][6] == 0x53
                 && MEETUP_MARKS[3][7] == 0x44 && MEETUP_MARKS[4][5] == 0x54 && MEETUP_MARKS[4][6] == 0x49 && MEETUP_MARKS[4][7] == 0x4d,
               "strong/meetup/header.h says other marks than these" );
static_assert( MEETUP_REGISTER == 1 && MEETUP_REGISTERED == 2 && MEETUP_PASS == 3 && MEETUP_PASSED == 4 && MEETUP_TIME == 5,
               "strong/meetup/header.h says other kinds than these" );

/*
 * True, and its header, if these bytes are one of the messages at its exact length. What is a
 * message, and where its parts are, is Strong::Meetup::Header's to say (strong/meetup/header.h),
 * and is proved; here its numbers are made the pointers and the text the rest of gosh takes. The
 * ticket is at 9 and what is sealed where the head says: both inside the datagram, of a head
 * that was read.
 */
inline bool meetup_read_header( const char* data, size_t n, MeetupHeader& h )
{
  Strong::Meetup::Header head;
  if ( !head.read_checked( Strong::Datagram( reinterpret_cast<const unsigned char*>( data ), n ) ) ) {
    return false;
  }
  h.kind = static_cast<MeetupKind>( head.kind_checked() );
  if ( h.kind == MEETUP_PASSED ) {
    h.ticket.clear();
  } else {
    h.ticket.assign( data + 9, 16 );
  }
  h.end = static_cast<int>( head.end_checked() );
  h.sealed = data + head.sealed_from_checked();
  h.sealed_len = n - head.sealed_from_checked();
  return true;
}

/* A message of this kind, sealed: counter (under 2^62) and direction make its nonce. */
inline std::string meetup_seal( Crypto::Session& session,
                                MeetupKind kind,
                                const std::string& ticket,
                                int end,
                                bool from_meetup,
                                uint64_t counter,
                                const std::string& text )
{
  std::string out( MEETUP_MARKS[kind - 1], 8 );
  out.push_back( static_cast<char>( MEETUP_VERSION ) );
  out += ticket;
  out.push_back( static_cast<char>( end ) );
  Strong::Meetup::Nonce stamp;
  stamp.set_checked( end ? 1 : 0, from_meetup ? 1 : 0, counter );
  out += session.encrypt(
    Crypto::Message( Crypto::Nonce( stamp.value_checked() ), std::string( 1, static_cast<char>( kind ) ) + text ) );
  return out;
}

/* Who may have sealed a meetup message, for the end its header names: the ticket's cipher, and whether the meetup. */
struct MeetupSealer
{
  Crypto::Session& session;
  bool by_meetup;
};

/* The kind of message whose text a T reads. */
template <typename T>
struct MeetupKindOf;
template <>
struct MeetupKindOf<Strong::Meetup::Register>
{
  static const MeetupKind kind = MEETUP_REGISTER;
};
template <>
struct MeetupKindOf<Strong::Meetup::Registered>
{
  static const MeetupKind kind = MEETUP_REGISTERED;
};
template <>
struct MeetupKindOf<Strong::Meetup::Pass>
{
  static const MeetupKind kind = MEETUP_PASS;
};
template <>
struct MeetupKindOf<Strong::Meetup::Time>
{
  static const MeetupKind kind = MEETUP_TIME;
};

/*
 * A meetup message of the kind a T reads, whose header was read. unseal opens it into `out` if the
 * seal opens under this cipher, and it was sealed as this kind, for the header's end, by whom it
 * should have been, owning what the cipher gave back, with no copy.
 */
template <typename T>
class Sealed<T, MeetupSealer>
{
private:
  const MeetupHeader& head;

public:
  explicit Sealed( const MeetupHeader& h ) : head( h ) {}

  bool unseal( const MeetupSealer& by, std::optional<Unsealed<T>>& out ) const
  {
    out.reset();
    if ( head.kind != MeetupKindOf<T>::kind || head.sealed_len < MEETUP_SEAL + 1 ) {
      return false;
    }
    try {
      out.emplace( typename Unsealed<T>::Key(), by.session, head.sealed, head.sealed_len, 1 );
    } catch ( const Crypto::CryptoException& ) {
      return false;
    }
    const std::string& text = out->message.text;
    Strong::Meetup::Nonce stamp;
    stamp.read_checked( out->message.nonce.val() );
    if ( stamp.end_checked() != ( head.end ? 1u : 0u ) || stamp.by_meetup_checked() != ( by.by_meetup ? 1u : 0u ) || text.empty()
         || static_cast<unsigned char>( text[0] ) != head.kind ) {
      out.reset();
      return false;
    }
    out->counter_of = stamp.counter_checked();
    return true;
  }
};

/* What is a REGISTER's text, and its time, is Strong::Meetup::Register's to say (strong/meetup/register.h),
   and is proved: true, and its time, if it reads. */
static_assert( MEETUP_WHERE_LEN == 19, "strong/meetup/register.h says another length of where than this" );
inline bool meetup_read_register( const Unsealed<Strong::Meetup::Register>& opened, uint64_t& time_ms )
{
  Strong::Meetup::Register r;
  if ( !opened.read( r ) ) {
    return false;
  }
  time_ms = r.time_checked();
  return true;
}

/* Whether a REGISTER that reads says it is from here: where, its first 19 bytes, compared where it lies. */
inline bool meetup_register_from( const Unsealed<Strong::Meetup::Register>& opened, const std::string& where )
{
  return where.size() == MEETUP_WHERE_LEN && opened.starts_with( where );
}

/* What is a REGISTERED's text is Strong::Meetup::Registered's to say (strong/meetup/registered.h), and is
   proved: true if it reads and says the end was seen where it believes it is, where's 19 bytes, compared
   where they lie. */
inline bool meetup_registered_at( const Unsealed<Strong::Meetup::Registered>& opened, const std::string& where )
{
  Strong::Meetup::Registered r;
  return where.size() == MEETUP_WHERE_LEN && opened.read( r ) && opened.starts_with( where );
}

/* What is a TIME's text is Strong::Meetup::Time's to say (strong/meetup/time.h), and is proved: true,
   the counter it answers and the meetup's time, if it reads. */
inline bool meetup_read_time( const Unsealed<Strong::Meetup::Time>& opened, uint64_t& answering, uint64_t& time_ms )
{
  Strong::Meetup::Time t;
  if ( !opened.read( t ) ) {
    return false;
  }
  answering = t.answering_checked();
  time_ms = t.time_checked();
  return true;
}

/* PASSED: what the meetup sends on. Not sealed: the datagram is the session's, sealed already. */
inline std::string meetup_passed( const std::string& datagram )
{
  return std::string( MEETUP_MARKS[MEETUP_PASSED - 1], 8 ) + std::string( 1, static_cast<char>( MEETUP_VERSION ) ) + datagram;
}

/* What is a PASS's text is Strong::Meetup::Pass's to say (strong/meetup/pass.h), and is proved: true,
   and out the PASSED that sends its datagram on, if it reads. The datagram is copied once, from where
   the cipher left it into out; nothing else is. */
static_assert( Strong::Meetup::DATAGRAM_MOST == MEETUP_MAX_DATAGRAM, "strong/meetup/pass.h says another most a datagram may be than this" );
inline bool meetup_passed( const Unsealed<Strong::Meetup::Pass>& opened, std::string& out )
{
  Strong::Meetup::Pass p;
  out.clear();
  out.reserve( 9 + MEETUP_MAX_DATAGRAM );
  out.append( MEETUP_MARKS[MEETUP_PASSED - 1], 8 );
  out.push_back( static_cast<char>( MEETUP_VERSION ) );
  return opened.read_onto( p, out );
}

}

#endif
