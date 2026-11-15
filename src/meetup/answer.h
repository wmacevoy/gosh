/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, answer: the STUN question, answered: "where did this request come from?" (REQ15/PATH-4,
 * REQ15/PATH-5). Logic: a request and its sender's address in, the answer's bytes out; and how
 * many a second. The statements are the fork's (meetup_serve).
 */

#ifndef GOSH_MEETUP_ANSWER_H
#define GOSH_MEETUP_ANSWER_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Gosh {

/* a Binding Request, with the cookie: and nothing longer than one need be */
inline bool is_stun_request( const char* request, size_t n )
{
  const unsigned char* p = reinterpret_cast<const unsigned char*>( request );
  return !( n < 20 || n > 200 || p[0] != 0x00 || p[1] != 0x01 || p[4] != 0x21 || p[5] != 0x12 || p[6] != 0xA4
            || p[7] != 0x42 );
}

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
