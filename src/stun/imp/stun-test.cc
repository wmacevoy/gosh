/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-3: STUN, asking (stun.h): the request's bytes, an answer read (XOR-MAPPED-ADDRESS,
 * and the older MAPPED-ADDRESS), what is not an answer refused, and addresses as text. One answer
 * is written by the meetup's answer.h and read here: the format is in two parts, and this keeps
 * them one format.
 */

#include <cstdio>
#include <string>

#include "src/meetup/imp/answer.h"
#include "src/meetup/imp/messages.h"
#include "src/stun/stun.h"
#include "vendor/facts/facts.h"
/* REQ1/PATH-3, REQ17/PATH-8: STUN, asking */
FACTS( Stun )
{
  using namespace Gosh::Stun;
  using Network::Addr;
  const std::string request = stun_request( "twelve bytes" );
  FACT_CHECK( request == std::string( "\x00\x01\x00\x00\x21\x12\xa4\x42", 8 ) + "twelve bytes", "a request: Binding Request, no attributes, the cookie, the id" );

  const std::string answer = Gosh::Meetup::Imp::stun_answer( request.data(), 0xCB007101, 7777 );
  Addr mapped;
  FACT_CHECK( ( stun_parse( answer.data(), answer.size(), mapped ) && format_ip_port( mapped ) == "203.0.113.1:7777" ), "the meetup's answer is read: where the asker was seen" );

  std::string older = std::string( "\x01\x01\x00\x0c\x21\x12\xa4\x42", 8 ) + "twelve bytes"
                      + std::string( "\x00\x01\x00\x08\x00\x01\x1e\x61\xc6\x33\x64\x02", 12 );
  FACT_CHECK( ( stun_parse( older.data(), older.size(), mapped ) && format_ip_port( mapped ) == "198.51.100.2:7777" ), "an answer from a server of an older kind (MAPPED-ADDRESS) is read too" );

  bool none = true;
  for ( size_t n = 0; n < answer.size(); n++ ) {
    none = none && !stun_parse( answer.data(), n, mapped );
  }
  FACT_CHECK( none, "an answer cut short anywhere is not read" );
  std::string wrong = answer;
  wrong[1] = 0x11;
  FACT_CHECK( !stun_parse( wrong.data(), wrong.size(), mapped ), "nor one that is not a success" );
  wrong = answer;
  wrong[5] = 0;
  FACT_CHECK( !stun_parse( wrong.data(), wrong.size(), mapped ), "nor one without our cookie" );
  wrong = answer;
  wrong[22] = 0x7f; /* an attribute's length past the end */
  FACT_CHECK( !stun_parse( wrong.data(), wrong.size(), mapped ), "nor one whose attribute runs past its end" );

  Addr a;
  FACT_CHECK( ( parse_ip_port( "1.2.3.4:567", a ) && format_ip_port( a ) == "1.2.3.4:567" ), "\"1.2.3.4:567\" is an address" );
  FACT_CHECK( ( !parse_ip_port( "1.2.3.4", a ) && !parse_ip_port( "1.2.3.4:0", a ) && !parse_ip_port( "1.2.3.4:65536", a )
           && !parse_ip_port( "host.example:22", a ) && !parse_ip_port( ":22", a ) && !parse_ip_port( "1.2.3.4:2x", a )
           && !parse_ip_port( "1.2.3.4:", a ) ), "what is not one is refused" );

  /* REQ17/PATH-8: IPv6. */
  unsigned char v6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x12, 0x34 };
  const std::string answer6 = Gosh::Meetup::Imp::stun_answer_to( request.data(), Gosh::Meetup::Imp::meetup_where_v6( v6, 7777 ) );
  FACT_CHECK( ( answer6.size() == 44 && answer6.find( std::string( reinterpret_cast<char*>( v6 ), 16 ) ) == std::string::npos ), "an IPv6 answer is 44 bytes, the address disguised with the cookie and the id (REQ17/PATH-8)" );
  FACT_CHECK( ( stun_parse( answer6.data(), answer6.size(), mapped ) && format_ip_port( mapped ) == "[2001:db8::1234]:7777" ), "and is read: where the asker was seen, by IPv6" );
  /* What a STUN server says is believed only as far as it can be written down and read back (found by
     the fuzzer under libFuzzer, 2026-11-16: an answer by IPv6 of one link, or of IPv4 in IPv6's form). */
  unsigned char mapped4[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 1, 2, 3, 4 };
  const std::string answer_m = Gosh::Meetup::Imp::stun_answer_to( request.data(), Gosh::Meetup::Imp::meetup_where_v6( mapped4, 7777 ) );
  FACT_CHECK( ( stun_parse( answer_m.data(), answer_m.size(), mapped ) && mapped.sa.sa_family == AF_INET
           && format_ip_port( mapped ) == "1.2.3.4:7777" ), "an answer of IPv4 in IPv6's form is read as the IPv4 address it is" );
  unsigned char link[16] = { 0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
  const std::string answer_l = Gosh::Meetup::Imp::stun_answer_to( request.data(), Gosh::Meetup::Imp::meetup_where_v6( link, 7777 ) );
  FACT_CHECK( !stun_parse( answer_l.data(), answer_l.size(), mapped ), "an answer of an IPv6 address of one link alone is not taken" );
  FACT_CHECK( ( parse_ip_port( "[2001:db8::1]:567", a ) && a.sa.sa_family == AF_INET6 && format_ip_port( a ) == "[2001:db8::1]:567" ), "\"[2001:db8::1]:567\" is an address, and goes back to the same text" );
  FACT_CHECK( ( !parse_ip_port( "2001:db8::1:567", a ) && !parse_ip_port( "[2001:db8::1:567", a ) && !parse_ip_port( "[]:567", a )
           && !parse_ip_port( "[1.2.3.4]:567", a ) ), "an IPv6 address without its brackets, or with one, is refused" );
  Addr b;
  parse_ip_port( "[2001:db8::1]:567", a );
  parse_ip_port( "[2001:db8::1]:568", b );
  FACT_CHECK( ( same_ip_port( a, a ) && !same_ip_port( a, b ) ), "two IPv6 addresses are the same only with the same port" );
  parse_ip_port( "1.2.3.4:567", b );
  FACT_CHECK( !same_ip_port( a, b ), "and an IPv4 one is never the same as an IPv6 one" );
  parse_ip_port( "0.0.0.0:567", b );
  FACT_CHECK( ( !same_ip_port( b, a ) && !same_ip_port( a, b ) ), "not even 0.0.0.0, whichever is asked about first" );
  /* (found by review, 2026-11-16: kept as IPv6, it never matched where the socket says a datagram came from) */
  FACT_CHECK( ( !parse_ip_port( "[fe80::1]:567", a ) && !parse_ip_port( "[fe80::1%eth0]:567", a ) ), "an address of one link alone is refused: it cannot be sent to without naming the link (REQ17/PATH-8)" );
  FACT_CHECK( ( parse_ip_port( "[::ffff:1.2.3.4]:567", a ) && a.sa.sa_family == AF_INET && parse_ip_port( "1.2.3.4:567", b )
           && same_ip_port( a, b ) && format_ip_port( a ) == "1.2.3.4:567" ), "an IPv4 address written in IPv6's form is taken as the IPv4 one it is" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Stun );
}

FACTS_MAIN
