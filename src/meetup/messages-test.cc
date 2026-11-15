/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ15/PATH-5: the messages between an end and a meetup (messages.h), with the real cipher: each
 * at its exact length, and none read, or opened, as anything it was not sealed as.
 */

#include <cstdio>
#include <string>

#include "src/meetup/messages.h"
#include "src/meetup/ticket.h"

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
  using namespace Gosh;
  const Ticket t = make_ticket( "0123456789abcdef", "random!!", 1000000 );
  Crypto::Session session( raw_key( t.key ) );
  Crypto::Session other( raw_key( "another key 16by" ) );
  const std::string where = meetup_where_v4( 0x7F000001, 60001 );

  check( "where: 19 bytes, family 4, the address and port most significant first",
         where.size() == 19 && where[0] == 4 && where.substr( 1, 4 ) == std::string( "\x7f\x00\x00\x01", 4 )
           && where.substr( 5, 12 ) == std::string( 12, '\0' ) && where.substr( 17 ) == "\xea\x61" );

  const std::string reg = meetup_seal( session, MEETUP_REGISTER, t.id, 1, false, 5, where );
  const std::string answer = meetup_seal( session, MEETUP_REGISTERED, t.id, 1, true, 0, where );
  check( "REGISTER and REGISTERED are each 70 bytes", reg.size() == 70 && answer.size() == 70 );

  MeetupHeader h;
  check( "REGISTER's header reads: its kind, its ticket, its end",
         meetup_read_header( reg.data(), reg.size(), h ) && h.kind == MEETUP_REGISTER && h.ticket == t.id && h.end == 1 );
  uint64_t counter = 0;
  std::string text;
  check( "and it opens, as sealed by end 1, to its counter and where",
         meetup_open( session, h, false, counter, text ) && counter == 5 && text == where );
  check( "not under another key", !meetup_open( other, h, false, counter, text ) );
  check( "not as sealed by the meetup", !meetup_open( session, h, true, counter, text ) );
  std::string as_end0 = reg;
  as_end0[25] = 0;
  check( "not as end 0's, if its end byte is changed",
         meetup_read_header( as_end0.data(), as_end0.size(), h ) && !meetup_open( session, h, false, counter, text ) );
  std::string as_pass = reg;
  as_pass.replace( 0, 8, MEETUP_MARKS[MEETUP_PASS - 1], 8 );
  check( "not as a PASS, if its mark is changed",
         meetup_read_header( as_pass.data(), as_pass.size(), h ) && !meetup_open( session, h, false, counter, text ) );

  bool none_reads = true;
  for ( size_t n = 0; n < 200; n++ ) {
    if ( n != 70 ) {
      std::string resized = reg;
      resized.resize( n, '\0' );
      none_reads = none_reads && !meetup_read_header( resized.data(), resized.size(), h );
    }
  }
  check( "a REGISTER of any length but 70 is not read", none_reads );
  std::string bad = reg;
  bad[8] = 2;
  check( "nor of another version", !meetup_read_header( bad.data(), bad.size(), h ) );
  bad = reg;
  bad[25] = 2;
  check( "nor for an end that is neither 0 nor 1", !meetup_read_header( bad.data(), bad.size(), h ) );
  bad = reg;
  bad[3] = 'X';
  check( "nor with a mark that is none of the four", !meetup_read_header( bad.data(), bad.size(), h ) );

  const std::string small = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 9, "x" );
  const std::string big = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 10, std::string( 1500, 'y' ) );
  const std::string too_big = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 11, std::string( 1501, 'y' ) );
  check( "a PASS is 51 bytes and its datagram: 52 to 1,551", small.size() == 52 && big.size() == 1551 );
  check( "both read and open to their datagrams",
         meetup_read_header( big.data(), big.size(), h ) && meetup_open( session, h, false, counter, text )
           && text == std::string( 1500, 'y' ) && counter == 10 );
  check( "one with a datagram over 1,500 bytes is not read", !meetup_read_header( too_big.data(), too_big.size(), h ) );
  const std::string empty = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 12, "" );
  check( "nor one with none", !meetup_read_header( empty.data(), empty.size(), h ) );

  const std::string passed = meetup_passed( std::string( 1500, 'y' ) );
  check( "PASSED is the mark, the version and the datagram: shorter than the PASS it came in",
         passed.size() == 1509 && passed.size() < big.size() && meetup_read_header( passed.data(), passed.size(), h )
           && h.kind == MEETUP_PASSED && std::string( h.sealed, h.sealed_len ) == std::string( 1500, 'y' ) );
  check( "PASSED is never opened", !meetup_open( session, h, true, counter, text ) );

  const std::string high = meetup_seal( session, MEETUP_PASS, t.id, 0, false, ~uint64_t( 0 ), "z" );
  check( "a counter is 62 bits: what is above them cannot change who sealed it",
         meetup_read_header( high.data(), high.size(), h ) && meetup_open( session, h, false, counter, text )
           && counter == MEETUP_COUNTER_MASK );

  /* REQ16/PATH-8: an IPv6 address as "where", and back. */
  Network::Addr a6, back;
  memset( &a6, 0, sizeof( a6 ) );
  a6.sin6.sin6_family = AF_INET6;
  a6.sin6.sin6_port = htons( 4242 );
  a6.sin6.sin6_addr.s6_addr[0] = 0x20;
  a6.sin6.sin6_addr.s6_addr[15] = 0x07;
  const std::string w6 = meetup_where_of( a6 );
  socklen_t len = 0;
  check( "an IPv6 address as where: family 6, its 16 bytes, its port; and back (REQ16/PATH-8)",
         w6.size() == 19 && w6[0] == 6 && w6[1] == 0x20 && w6[16] == 0x07 && meetup_addr_of( w6, back, len )
           && back.sa.sa_family == AF_INET6 && len == sizeof( back.sin6 ) && memcmp( &back.sin6.sin6_addr, &a6.sin6.sin6_addr, 16 ) == 0
           && back.sin6.sin6_port == a6.sin6.sin6_port );
  Network::Addr mapped = a6;
  memset( mapped.sin6.sin6_addr.s6_addr, 0, 16 );
  mapped.sin6.sin6_addr.s6_addr[10] = 0xff;
  mapped.sin6.sin6_addr.s6_addr[11] = 0xff;
  mapped.sin6.sin6_addr.s6_addr[12] = 127;
  mapped.sin6.sin6_addr.s6_addr[15] = 1;
  check( "an IPv4 address written in IPv6's form is taken as the IPv4 one it is",
         meetup_where_of( mapped ) == meetup_where_v4( 0x7F000001, 4242 ) );
  check( "where of another family is no address", !meetup_addr_of( std::string( 19, '\x05' ), back, len ) );

  if ( failures ) {
    printf( "FAIL REQ15/PATH-5, REQ16/PATH-8: %d checks of the meetup's messages failed\n", failures );
    return 1;
  }
  printf( "PASS REQ15/PATH-5, REQ16/PATH-8: the meetup's messages\n" );
  return 0;
}
