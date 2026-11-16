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
#include "src/meetup/messages.h"
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

  /* REQ17/PATH-8: IPv6. */
  unsigned char v6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x12, 0x34 };
  const std::string answer6 = Gosh::stun_answer_to( request.data(), Gosh::meetup_where_v6( v6, 7777 ) );
  check( "an IPv6 answer is 44 bytes, the address disguised with the cookie and the id (REQ17/PATH-8)",
         answer6.size() == 44 && answer6.find( std::string( reinterpret_cast<char*>( v6 ), 16 ) ) == std::string::npos );
  check( "and is read: where the asker was seen, by IPv6",
         stun_parse( answer6.data(), answer6.size(), mapped ) && format_ip_port( mapped ) == "[2001:db8::1234]:7777" );
  /* What a STUN server says is believed only as far as it can be written down and read back (found by
     the fuzzer under libFuzzer, 2026-11-16: an answer by IPv6 of one link, or of IPv4 in IPv6's form). */
  unsigned char mapped4[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 1, 2, 3, 4 };
  const std::string answer_m = Gosh::stun_answer_to( request.data(), Gosh::meetup_where_v6( mapped4, 7777 ) );
  check( "an answer of IPv4 in IPv6's form is read as the IPv4 address it is",
         stun_parse( answer_m.data(), answer_m.size(), mapped ) && mapped.sa.sa_family == AF_INET
           && format_ip_port( mapped ) == "1.2.3.4:7777" );
  unsigned char link[16] = { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
  const std::string answer_l = Gosh::stun_answer_to( request.data(), Gosh::meetup_where_v6( link, 7777 ) );
  check( "an answer of an IPv6 address of one link alone is not taken", !stun_parse( answer_l.data(), answer_l.size(), mapped ) );
  check( "\"[2001:db8::1]:567\" is an address, and goes back to the same text",
         parse_ip_port( "[2001:db8::1]:567", a ) && a.sa.sa_family == AF_INET6 && format_ip_port( a ) == "[2001:db8::1]:567" );
  check( "an IPv6 address without its brackets, or with one, is refused",
         !parse_ip_port( "2001:db8::1:567", a ) && !parse_ip_port( "[2001:db8::1:567", a ) && !parse_ip_port( "[]:567", a )
           && !parse_ip_port( "[1.2.3.4]:567", a ) );
  Addr b;
  parse_ip_port( "[2001:db8::1]:567", a );
  parse_ip_port( "[2001:db8::1]:568", b );
  check( "two IPv6 addresses are the same only with the same port", same_ip_port( a, a ) && !same_ip_port( a, b ) );
  parse_ip_port( "1.2.3.4:567", b );
  check( "and an IPv4 one is never the same as an IPv6 one", !same_ip_port( a, b ) );
  parse_ip_port( "0.0.0.0:567", b );
  check( "not even 0.0.0.0, whichever is asked about first", !same_ip_port( b, a ) && !same_ip_port( a, b ) );
  /* (found by review, 2026-11-16: kept as IPv6, it never matched where the socket says a datagram came from) */
  check( "an address of one link alone is refused: it cannot be sent to without naming the link (REQ17/PATH-8)",
         !parse_ip_port( "[fe80::1]:567", a ) && !parse_ip_port( "[fe80::1%eth0]:567", a ) );
  check( "an IPv4 address written in IPv6's form is taken as the IPv4 one it is",
         parse_ip_port( "[::ffff:1.2.3.4]:567", a ) && a.sa.sa_family == AF_INET && parse_ip_port( "1.2.3.4:567", b )
           && same_ip_port( a, b ) && format_ip_port( a ) == "1.2.3.4:567" );

  if ( failures ) {
    printf( "FAIL REQ1/PATH-3, REQ17/PATH-8: %d checks of STUN failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/PATH-3, REQ17/PATH-8: STUN, asking\n" );
  return 0;
}
