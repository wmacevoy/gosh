/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ15/PATH-4: the STUN question answered (answer.h), with bytes and numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/meetup/imp/answer.h"
#include "vendor/facts/facts.h"
/* REQ15/PATH-4: the STUN question answered */
FACTS( Answer )
{
  /* a Binding Request: type 0x0001, length 0, the cookie, twelve bytes of the asker's */
  const std::string request = std::string( "\x00\x01\x00\x00\x21\x12\xA4\x42", 8 ) + "abcdefghijkl";
  FACT_CHECK( Gosh::Stun::is_stun_request( request.data(), request.size() ), "a Binding Request with the cookie is one" );
  FACT_CHECK( ( !Gosh::Stun::is_stun_request( request.data(), 19 ) && !Gosh::Stun::is_stun_request( ( request + std::string( 181, 'x' ) ).data(), 201 )
           && Gosh::Stun::is_stun_request( ( request + std::string( 180, 'x' ) ).data(), 200 ) ), "one byte short is not; nor 201 bytes long; 200 is" );
  std::string other = request;
  other[1] = 0x11;
  std::string uncooked = request;
  uncooked[7] = 0x43;
  FACT_CHECK( ( !Gosh::Stun::is_stun_request( other.data(), other.size() ) && !Gosh::Stun::is_stun_request( uncooked.data(), uncooked.size() ) ), "another type, or without the cookie, is not" );

  /* asked from 192.0.2.7 port 40000: the answer says so, each number mixed with the cookie */
  const std::string answer = Gosh::Meetup::Imp::stun_answer( request.data(), 0xC0000207, 40000 );
  const std::string expected = std::string( "\x01\x01\x00\x0c\x21\x12\xA4\x42", 8 ) + "abcdefghijkl"
                               + std::string( "\x00\x20\x00\x08\x00\x01", 6 )
                               + std::string( "\xBD\x52", 2 )          /* 40000 ^ 0x2112 */
                               + std::string( "\xE1\x12\xA6\x45", 4 ); /* 192.0.2.7 ^ 0x2112A442 */
  FACT_CHECK( answer == expected, "the answer: a Binding Response with the asker's twelve bytes, and where the request came from" );

  Gosh::Meetup::Imp::AnswerRate rate;
  unsigned int allowed = 0;
  for ( int i = 0; i < 500; i++ ) {
    allowed += rate.allow( 1000 ) ? 1 : 0;
  }
  FACT_CHECK( allowed == 200, "500 asked in one second: 200 answered" );
  FACT_CHECK( rate.allow( 1001 ), "in the next second, answering again" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Answer );
}

FACTS_MAIN
