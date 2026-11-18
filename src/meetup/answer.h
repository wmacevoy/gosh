/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, answer: the STUN question, answered: "where did this request come from?" (REQ15/PATH-4,
 * REQ18/PATH-5). Logic: a request and its sender's address in, the answer's bytes out; and how
 * many a second. The statements are the fork's (meetup_serve).
 */

#ifndef GOSH_MEETUP_ANSWER_H
#define GOSH_MEETUP_ANSWER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "src/strong/stunrequest.h"

namespace Gosh {

/* (strong/stunrequest.h; under this name too, for those who ask it here) */
using Strong::is_stun_request_checked;

/*
 * The answer to a request that came from this IPv4 address and port (both as numbers, in the
 * machine's own order). XOR-MAPPED-ADDRESS: where it came from, disguised from anything in
 * between that rewrites addresses it sees.
 */
inline std::string stun_answer( const char* request, uint32_t address, uint16_t port )
{
  const uint32_t STUN_COOKIE = 0x2112A442;
  const uint16_t port_x = port ^ ( STUN_COOKIE >> 16 );
  const uint32_t ip_x = address ^ STUN_COOKIE;
  std::string answer;
  answer.push_back( 0x01 );
  answer.push_back( 0x01 );
  answer.push_back( 0x00 );
  answer.push_back( 0x0c );
  answer.append( request + 4, 16 );
  const char attribute[4] = { 0x00, 0x20, 0x00, 0x08 };
  answer.append( attribute, 4 );
  answer.push_back( 0x00 );
  answer.push_back( 0x01 );
  answer.push_back( static_cast<char>( port_x >> 8 ) );
  answer.push_back( static_cast<char>( port_x & 0xFF ) );
  for ( int shift = 24; shift >= 0; shift -= 8 ) {
    answer.push_back( static_cast<char>( ( ip_x >> shift ) & 0xFF ) );
  }
  return answer;
}

/*
 * (REQ17/PATH-8) The answer to a request from "where" (messages.h), IPv4 or IPv6. For IPv6 the
 * address is disguised with the cookie and the request's own id (RFC 8489, 14.2). Empty if
 * "where" is neither.
 */
inline std::string stun_answer_to( const char* request, const std::string& where )
{
  if ( where.size() != 19 || ( where[0] != 4 && where[0] != 6 ) ) {
    return std::string();
  }
  const uint16_t port = ( static_cast<unsigned char>( where[17] ) << 8 ) | static_cast<unsigned char>( where[18] );
  if ( where[0] == 4 ) {
    const uint32_t address = ( uint32_t( static_cast<unsigned char>( where[1] ) ) << 24 ) | ( static_cast<unsigned char>( where[2] ) << 16 )
                             | ( static_cast<unsigned char>( where[3] ) << 8 ) | static_cast<unsigned char>( where[4] );
    return stun_answer( request, address, port );
  }
  const uint16_t port_x = port ^ ( 0x2112A442 >> 16 );
  std::string answer;
  answer.push_back( 0x01 );
  answer.push_back( 0x01 );
  answer.push_back( 0x00 );
  answer.push_back( 0x18 );
  answer.append( request + 4, 16 );
  const char attribute[4] = { 0x00, 0x20, 0x00, 0x14 };
  answer.append( attribute, 4 );
  answer.push_back( 0x00 );
  answer.push_back( 0x02 );
  answer.push_back( static_cast<char>( port_x >> 8 ) );
  answer.push_back( static_cast<char>( port_x & 0xFF ) );
  for ( int i = 0; i < 16; i++ ) {
    /* the cookie and the id are request bytes 4 to 19 */
    answer.push_back( static_cast<char>( where[1 + i] ^ request[4 + i] ) );
  }
  return answer;
}

/* No more than a few hundred answers a second, so as to be no use to anyone for anything else. */
class AnswerRate
{
private:
  uint64_t second;
  unsigned int answered;

public:
  static constexpr unsigned int PER_SECOND = 200;

  AnswerRate() : second( 0 ), answered( 0 ) {}

  /* Whether another may be answered in this second (the time in seconds). Asking counts as one. */
  bool allow( uint64_t now )
  {
    if ( now != second ) {
      second = now;
      answered = 0;
    }
    return ++answered <= PER_SECOND;
  }
};

}

#endif
