/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/PATH-4: the STUN question answered (answer.h), with bytes and numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/meetup/answer.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-90s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  /* a Binding Request: type 0x0001, length 0, the cookie, twelve bytes of the asker's */
  const std::string request = std::string( "\x00\x01\x00\x00\x21\x12\xA4\x42", 8 ) + "abcdefghijkl";
  check( "a Binding Request with the cookie is one", Gosh::is_stun_request( request.data(), request.size() ) );
  check( "one byte short is not; nor 201 bytes long; 200 is",
         !Gosh::is_stun_request( request.data(), 19 ) && !Gosh::is_stun_request( ( request + std::string( 181, 'x' ) ).data(), 201 )
           && Gosh::is_stun_request( ( request + std::string( 180, 'x' ) ).data(), 200 ) );
  std::string other = request;
  other[1] = 0x11;
  std::string uncooked = request;
  uncooked[7] = 0x43;
  check( "another type, or without the cookie, is not",
         !Gosh::is_stun_request( other.data(), other.size() ) && !Gosh::is_stun_request( uncooked.data(), uncooked.size() ) );

  /* asked from 192.0.2.7 port 40000: the answer says so, each number mixed with the cookie */
  const std::string answer = Gosh::stun_answer( request.data(), 0xC0000207, 40000 );
  const std::string expected = std::string( "\x01\x01\x00\x0c\x21\x12\xA4\x42", 8 ) + "abcdefghijkl"
                               + std::string( "\x00\x20\x00\x08\x00\x01", 6 )
                               + std::string( "\xBD\x52", 2 )          /* 40000 ^ 0x2112 */
                               + std::string( "\xE1\x12\xA6\x45", 4 ); /* 192.0.2.7 ^ 0x2112A442 */
  check( "the answer: a Binding Response with the asker's twelve bytes, and where the request came from", answer == expected );

  Gosh::AnswerRate rate;
  unsigned int allowed = 0;
  for ( int i = 0; i < 500; i++ ) {
    allowed += rate.allow( 1000 ) ? 1 : 0;
  }
  check( "500 asked in one second: 200 answered", allowed == 200 );
  check( "in the next second, answering again", rate.allow( 1001 ) );

  if ( failures ) {
    printf( "FAIL REQ1/PATH-4: %d checks of the STUN answer failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/PATH-4: the STUN question answered\n" );
  return EXIT_SUCCESS;
}
