/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-3: STUN, asking (stun.h): the request's bytes, an answer read (XOR-MAPPED-ADDRESS,
 * and the older MAPPED-ADDRESS), what is not an answer refused, and addresses as text. One answer
 * is written by the meetup's answer.h and read here: the format is in two parts, and this keeps
 * them one format.
 */

#include <cstdio>
#include <string>

#include "src/meetup/answer.h"
#include "src/pathfinding/stun.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

int main()
{
  using namespace Network;
  const std::string request = stun_request( "twelve bytes" );
  check( "a request: Binding Request, no attributes, the cookie, the id",
         request == std::string( "\x00\x01\x00\x00\x21\x12\xa4\x42", 8 ) + "twelve bytes" );

  const std::string answer = Gosh::stun_answer( request.data(), 0xCB007101, 7777 );
  Addr mapped;
  check( "the meetup's answer is read: where the asker was seen",
         stun_parse( answer.data(), answer.size(), mapped ) && format_ip_port( mapped ) == "203.0.113.1:7777" );

  std::string older = std::string( "\x01\x01\x00\x0c\x21\x12\xa4\x42", 8 ) + "twelve bytes"
                      + std::string( "\x00\x01\x00\x08\x00\x01\x1e\x61\xc6\x33\x64\x02", 12 );
  check( "an answer from a server of an older kind (MAPPED-ADDRESS) is read too",
         stun_parse( older.data(), older.size(), mapped ) && format_ip_port( mapped ) == "198.51.100.2:7777" );

  bool none = true;
  for ( size_t n = 0; n < answer.size(); n++ ) {
    none = none && !stun_parse( answer.data(), n, mapped );
  }
  check( "an answer cut short anywhere is not read", none );
  std::string wrong = answer;
  wrong[1] = 0x11;
  check( "nor one that is not a success", !stun_parse( wrong.data(), wrong.size(), mapped ) );
  wrong = answer;
  wrong[5] = 0;
  check( "nor one without our cookie", !stun_parse( wrong.data(), wrong.size(), mapped ) );
  wrong = answer;
  wrong[22] = 0x7f; /* an attribute's length past the end */
  check( "nor one whose attribute runs past its end", !stun_parse( wrong.data(), wrong.size(), mapped ) );

  Addr a;
  check( "\"1.2.3.4:567\" is an address", parse_ip_port( "1.2.3.4:567", a ) && format_ip_port( a ) == "1.2.3.4:567" );
  check( "what is not one is refused",
         !parse_ip_port( "1.2.3.4", a ) && !parse_ip_port( "1.2.3.4:0", a ) && !parse_ip_port( "1.2.3.4:65536", a )
           && !parse_ip_port( "host.example:22", a ) && !parse_ip_port( ":22", a ) && !parse_ip_port( "1.2.3.4:2x", a )
           && !parse_ip_port( "1.2.3.4:", a ) );

  if ( failures ) {
    printf( "FAIL REQ1/PATH-3: %d checks of STUN failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/PATH-3: STUN, asking\n" );
  return 0;
}
