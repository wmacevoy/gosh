/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ18/PATH-5: the messages between an end and a meetup (messages.h), with the real cipher: each
 * at its exact length, and none read, or opened, as anything it was not sealed as.
 */

#include <cstdio>
#include <string>

#include "src/meetup/messages.h"
#include "src/meetup/ticket.h"
#include "src/seam/proved.h"
#include "vendor/facts/facts.h"
/* REQ18/PATH-5, REQ17/PATH-8: the meetup's messages */
FACTS( Messages )
{
  using namespace Gosh;
  const Ticket t = make_ticket( "0123456789abcdef", "random!!", 1000000 );
  Crypto::Session session( raw_key( t.key ) );
  Crypto::Session other( raw_key( "another key 16by" ) );
  const std::string where = meetup_where_v4( 0x7F000001, 60001 );

  FACT_CHECK( ( where.size() == 19 && where[0] == 4 && where.substr( 1, 4 ) == std::string( "\x7f\x00\x00\x01", 4 )
           && where.substr( 5, 12 ) == std::string( 12, '\0' ) && where.substr( 17 ) == "\xea\x61" ), "where: 19 bytes, family 4, the address and port most significant first" );

  const std::string said = meetup_register_text( where, 1763337600123ULL );
  const std::string reg = meetup_seal( session, MEETUP_REGISTER, t.id, 1, false, 5, said );
  const std::string answer = meetup_seal( session, MEETUP_REGISTERED, t.id, 1, true, 0, where );
  FACT_CHECK( ( reg.size() == 78 && answer.size() == 70 ), "REGISTER is 78 bytes and REGISTERED 70 (REQ18/PATH-5: REGISTER carries a time)" );
  uint64_t time_back = 0;
  MeetupHeader rh;
  std::optional<Unsealed<Strong::Meetup::Register>> opened;
  FACT_CHECK( ( said.size() == 27 && said.substr( 19 ) == std::string( "\x00\x00\x01\x9a\x8f\x1c\x7c\x7b", 8 )
           && meetup_read_header( reg.data(), reg.size(), rh ) && Sealed<Strong::Meetup::Register, MeetupSealer>( rh ).unseal( MeetupSealer{ session, false }, opened )
           && opened->counter() == 5 && meetup_read_register( *opened, time_back ) && meetup_register_from( *opened, where )
           && time_back == 1763337600123ULL ), "REGISTER's text: where, then the time, eight bytes most significant first, read where the cipher left it" );
  FACT_CHECK( !meetup_register_from( *opened, meetup_where_v4( 0x7F000001, 60002 ) ) && !meetup_register_from( *opened, "" ), "and it is from where it says, and nowhere else" );
  FACT_CHECK( ( !Sealed<Strong::Meetup::Register, MeetupSealer>( rh ).unseal( MeetupSealer{ other, false }, opened ) && !opened
           && !Sealed<Strong::Meetup::Register, MeetupSealer>( rh ).unseal( MeetupSealer{ session, true }, opened ) && !opened ), "a REGISTER does not unseal under another key, or as sealed by the meetup" );
  const std::string short_reg = meetup_seal( session, MEETUP_REGISTER, t.id, 1, false, 6, where );
  MeetupHeader sh;
  FACT_CHECK( ( !meetup_read_header( short_reg.data(), short_reg.size(), sh ) ), "a REGISTER whose text is of another length is not even read as one" );
  const std::string told = meetup_seal( session, MEETUP_TIME, t.id, 1, true, 1, meetup_time_text( 5, 1763337600000ULL ) );
  MeetupHeader th;
  uint64_t answering = 0, its_time = 0;
  std::optional<Unsealed<Strong::Meetup::Time>> tt;
  FACT_CHECK( ( told.size() == 67 && meetup_read_header( told.data(), told.size(), th ) && th.kind == MEETUP_TIME
           && Sealed<Strong::Meetup::Time, MeetupSealer>( th ).unseal( MeetupSealer{ session, true }, tt )
           && meetup_read_time( *tt, answering, its_time ) && answering == 5 && its_time == 1763337600000ULL && tt->counter() == 1 ),
           "TIME is 67 bytes, and opens, as the meetup's, to the counter it answers and the meetup's time" );
  FACT_CHECK( ( !Sealed<Strong::Meetup::Time, MeetupSealer>( th ).unseal( MeetupSealer{ session, false }, tt ) && !tt ),
              "a TIME does not unseal as sealed by an end" );
  FACT_CHECK( ( !Sealed<Strong::Meetup::Register, MeetupSealer>( th ).unseal( MeetupSealer{ session, true }, opened ) && !opened ),
              "nor as a REGISTER" );
  const std::string beyond = meetup_seal( session, MEETUP_TIME, t.id, 1, true, 2, meetup_time_text( Strong::Meetup::COUNTER_MOST + 1, 1 ) );
  const std::string most = meetup_seal( session, MEETUP_TIME, t.id, 1, true, 3, meetup_time_text( Strong::Meetup::COUNTER_MOST, 1 ) );
  MeetupHeader bh, mh;
  FACT_CHECK( ( meetup_read_header( beyond.data(), beyond.size(), bh ) && Sealed<Strong::Meetup::Time, MeetupSealer>( bh ).unseal( MeetupSealer{ session, true }, tt )
           && !meetup_read_time( *tt, answering, its_time ) ), "a TIME that answers a counter of 2^62, which no REGISTER has, opens and is not read" );
  FACT_CHECK( ( meetup_read_header( most.data(), most.size(), mh ) && Sealed<Strong::Meetup::Time, MeetupSealer>( mh ).unseal( MeetupSealer{ session, true }, tt )
           && meetup_read_time( *tt, answering, its_time ) && answering == Strong::Meetup::COUNTER_MOST ), "one that answers 2^62 - 1 is read" );
  bool no_time_reads = true;
  for ( size_t n = 0; n < 100; n++ ) {
    if ( n != 67 ) {
      std::string resized = told;
      resized.resize( n, '\0' );
      no_time_reads = no_time_reads && !( meetup_read_header( resized.data(), resized.size(), th ) && th.kind == MEETUP_TIME );
    }
  }
  FACT_CHECK( no_time_reads, "a TIME of any length but 67 is not read as one" );

  MeetupHeader h;
  FACT_CHECK( ( meetup_read_header( reg.data(), reg.size(), h ) && h.kind == MEETUP_REGISTER && h.ticket == t.id && h.end == 1 ), "REGISTER's header reads: its kind, its ticket, its end" );
  uint64_t counter = 0;
  std::string text;
  FACT_CHECK( ( meetup_open( session, h, false, counter, text ) && counter == 5 && text == said ), "and it opens, as sealed by end 1, to its counter and where" );
  FACT_CHECK( !meetup_open( other, h, false, counter, text ), "not under another key" );
  FACT_CHECK( !meetup_open( session, h, true, counter, text ), "not as sealed by the meetup" );
  std::string as_end0 = reg;
  as_end0[25] = 0;
  FACT_CHECK( ( meetup_read_header( as_end0.data(), as_end0.size(), h ) && !meetup_open( session, h, false, counter, text ) ), "not as end 0's, if its end byte is changed" );
  std::string as_pass = reg;
  as_pass.replace( 0, 8, MEETUP_MARKS[MEETUP_PASS - 1], 8 );
  FACT_CHECK( ( meetup_read_header( as_pass.data(), as_pass.size(), h ) && !meetup_open( session, h, false, counter, text ) ), "not as a PASS, if its mark is changed" );

  bool none_reads = true;
  for ( size_t n = 0; n < 200; n++ ) {
    if ( n != 78 ) {
      std::string resized = reg;
      resized.resize( n, '\0' );
      none_reads = none_reads && !meetup_read_header( resized.data(), resized.size(), h );
    }
  }
  FACT_CHECK( none_reads, "a REGISTER of any length but 78 is not read" );
  std::string bad = reg;
  bad[8] = 1;
  FACT_CHECK( !meetup_read_header( bad.data(), bad.size(), h ), "nor of another version (1: before REGISTER carried a time)" );
  bad = reg;
  bad[25] = 2;
  FACT_CHECK( !meetup_read_header( bad.data(), bad.size(), h ), "nor for an end that is neither 0 nor 1" );
  bad = reg;
  bad[3] = 'X';
  FACT_CHECK( !meetup_read_header( bad.data(), bad.size(), h ), "nor with a mark that is none of the five" );

  const std::string small = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 9, "x" );
  const std::string big = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 10, std::string( 1500, 'y' ) );
  const std::string too_big = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 11, std::string( 1501, 'y' ) );
  FACT_CHECK( ( small.size() == 52 && big.size() == 1551 ), "a PASS is 51 bytes and its datagram: 52 to 1,551" );
  FACT_CHECK( ( meetup_read_header( big.data(), big.size(), h ) && meetup_open( session, h, false, counter, text )
           && text == std::string( 1500, 'y' ) && counter == 10 ), "both read and open to their datagrams" );
  FACT_CHECK( !meetup_read_header( too_big.data(), too_big.size(), h ), "one with a datagram over 1,500 bytes is not read" );
  const std::string empty = meetup_seal( session, MEETUP_PASS, t.id, 0, false, 12, "" );
  FACT_CHECK( !meetup_read_header( empty.data(), empty.size(), h ), "nor one with none" );

  const std::string passed = meetup_passed( std::string( 1500, 'y' ) );
  FACT_CHECK( ( passed.size() == 1509 && passed.size() < big.size() && meetup_read_header( passed.data(), passed.size(), h )
           && h.kind == MEETUP_PASSED && std::string( h.sealed, h.sealed_len ) == std::string( 1500, 'y' ) ), "PASSED is the mark, the version and the datagram: shorter than the PASS it came in" );
  FACT_CHECK( !meetup_open( session, h, true, counter, text ), "PASSED is never opened" );

  /* (proved of every counter and sender; the seal and the open around it are tried by every fact above) */
  if ( !proved_so_not_tried( "a counter is 62 bits: what is above them cannot change who sealed it",
                             "Strong::Meetup::Nonce, proofs/MeetupNonce.proof.cpp" ) ) {
    const std::string high = meetup_seal( session, MEETUP_PASS, t.id, 0, false, ~uint64_t( 0 ), "z" );
    FACT_CHECK( ( meetup_read_header( high.data(), high.size(), h ) && meetup_open( session, h, false, counter, text )
             && counter == MEETUP_COUNTER_MASK ), "a counter is 62 bits: what is above them cannot change who sealed it" );
  }

  /* REQ17/PATH-8: an IPv6 address as "where", and back. */
  Network::Addr a6, back;
  memset( &a6, 0, sizeof( a6 ) );
  a6.sin6.sin6_family = AF_INET6;
  a6.sin6.sin6_port = htons( 4242 );
  a6.sin6.sin6_addr.s6_addr[0] = 0x20;
  a6.sin6.sin6_addr.s6_addr[15] = 0x07;
  const std::string w6 = meetup_where_of( a6 );
  socklen_t len = 0;
  FACT_CHECK( ( w6.size() == 19 && w6[0] == 6 && w6[1] == 0x20 && w6[16] == 0x07 && meetup_addr_of( w6, back, len )
           && back.sa.sa_family == AF_INET6 && len == sizeof( back.sin6 ) && memcmp( &back.sin6.sin6_addr, &a6.sin6.sin6_addr, 16 ) == 0
           && back.sin6.sin6_port == a6.sin6.sin6_port ), "an IPv6 address as where: family 6, its 16 bytes, its port; and back (REQ17/PATH-8)" );
  Network::Addr mapped = a6;
  memset( mapped.sin6.sin6_addr.s6_addr, 0, 16 );
  mapped.sin6.sin6_addr.s6_addr[10] = 0xff;
  mapped.sin6.sin6_addr.s6_addr[11] = 0xff;
  mapped.sin6.sin6_addr.s6_addr[12] = 127;
  mapped.sin6.sin6_addr.s6_addr[15] = 1;
  FACT_CHECK( meetup_where_of( mapped ) == meetup_where_v4( 0x7F000001, 4242 ), "an IPv4 address written in IPv6's form is taken as the IPv4 one it is" );
  FACT_CHECK( !meetup_addr_of( std::string( 19, '\x05' ), back, len ), "where of another family is no address" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Messages );
}

FACTS_MAIN
