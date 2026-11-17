/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/FWD-2: a datagram on its way out, as one payload or as fragments (sending.h), with numbers. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/udp/sending.h"
#include "vendor/facts/facts.h"



struct Sent
{
  std::vector<std::string> payloads;
  void send( const std::string& payload ) { payloads.push_back( payload ); }

  /* The kinds and the sequence numbers of what was sent. */
  std::vector<int> kinds( void ) const
  {
    std::vector<int> found;
    for ( size_t i = 0; i < payloads.size(); i++ ) {
      found.push_back( static_cast<uint8_t>( payloads[i][1] ) );
    }
    return found;
  }
  std::vector<uint64_t> numbers( void ) const
  {
    std::vector<uint64_t> found;
    for ( size_t i = 0; i < payloads.size(); i++ ) {
      uint8_t type, forward;
      uint16_t id;
      uint64_t seq;
      std::string data;
      Gosh::decode_frame( payloads[i], type, forward, id, seq, data );
      found.push_back( seq );
    }
    return found;
  }
};

static Gosh::DatagramLink link_of( size_t max_payload, double loss = 0, bool ready = true )
{
  Gosh::DatagramLink link;
  link.ready = ready;
  link.max_payload = max_payload;
  link.loss = loss;
  return link;
}
/* REQ1/FWD-2: a UDP forward's sending, with no socket */
FACTS( Sending )
{
  using Gosh::Sending;
  const std::string big( 1000, 'x' );
  typedef std::vector<int> Kinds;
  typedef std::vector<uint64_t> Numbers;

  {
    Sending s( false );
    Sent out;
    s.send( 3, 0xBEEF, big.data(), 87, link_of( 100 ), out );
    uint8_t type = 9, forward = 9;
    uint16_t id = 0;
    uint64_t seq = 9;
    std::string data;
    FACT_CHECK( ( out.payloads.size() == 1 && out.payloads[0].size() == 100 ), "87 bytes where a payload holds 100: one payload of exactly 100 (13 of header)" );
    FACT_CHECK( ( Gosh::decode_frame( out.payloads[0], type, forward, id, seq, data ) && type == Gosh::TYPE_UDP_DATA && forward == 3
             && id == 0xBEEF && seq == 0 && data == big.substr( 0, 87 ) ), "framed as plain UDP data, for its forward and conversation, numbered 0" );
    s.set_parity_mode( Sending::PARITY_NEVER );
    s.send( 3, 0xBEEF, big.data(), 88, link_of( 100 ), out );
    FACT_CHECK( ( out.kinds() == Kinds( { 0, 2, 2 } ) && s.get_fragmented() == 1 ), "88 bytes: two fragments, of the fragment kind" );
    s.send( 3, 0xBEEF, big.data(), 10, link_of( 100 ), out );
    FACT_CHECK( out.numbers() == Numbers( { 0, 1, 2, 3 } ), "everything sent is numbered one after another, fragments too" );
  }
  {
    Sending s( true );
    Sent out;
    s.set_parity_mode( Sending::PARITY_NEVER );
    s.send( 0, 1, big.data(), 10, link_of( 100 ), out );
    s.send( 0, 1, big.data(), 200, link_of( 100 ), out );
    FACT_CHECK( out.kinds() == Kinds( { 4, 5, 5, 5 } ), "forwards the server listens for use their own two kinds" );
  }
  {
    /* 213 bytes a payload: 200 for a fragment, 196 of them the datagram's. 1,000 bytes is 6 pieces. */
    Sending s( false );
    Sent out;
    s.set_parity_mode( Sending::PARITY_NEVER );
    s.send( 0, 1, big.data(), 1000, link_of( 213, 0.5 ), out );
    FACT_CHECK( ( out.payloads.size() == 6 && s.get_with_parity() == 0 ), "never: six fragments, however lossy the session" );
    out.payloads.clear();
    s.set_parity_mode( Sending::PARITY_ALWAYS );
    s.send( 0, 1, big.data(), 1000, link_of( 213, 0 ), out );
    FACT_CHECK( ( out.payloads.size() == 7 && s.get_with_parity() == 1 && s.get_fragmented() == 2 ), "always: seven, one of them parity, though nothing is being lost" );
    out.payloads.clear();
    s.set_parity_mode( Sending::PARITY_AUTO );
    s.send( 0, 1, big.data(), 1000, link_of( 213, 0.003 ), out ); /* six fragments: lost 1.8 times in 100 */
    FACT_CHECK( out.payloads.size() == 6, "left to decide, at 0.3% loss: six" );
    out.payloads.clear();
    s.send( 0, 1, big.data(), 1000, link_of( 213, 0.004 ), out ); /* 2.4 times in 100 */
    FACT_CHECK( out.payloads.size() == 7, "at 0.4%: seven" );
  }
  {
    Sending s( false );
    Sent out;
    s.send( 0, 1, big.data(), 10, link_of( 100, 0, false ), out );
    FACT_CHECK( out.payloads.empty(), "the session not ready: nothing is sent" );
    s.set_parity_mode( Sending::PARITY_NEVER );
    const std::string huge( 22601, 'y' );
    s.send( 0, 1, huge.data(), huge.size(), link_of( 13 + 4 + 100 ), out );
    FACT_CHECK( ( out.payloads.empty() && s.get_dropped_oversize() == 1 ), "more than 226 pieces: dropped, and counted" );
    s.send( 0, 1, big.data(), 10, link_of( 13 ), out );
    FACT_CHECK( ( out.payloads.empty() && s.get_dropped_oversize() == 2 ), "a payload too small to hold anything but the header: dropped, and counted" );
    s.send( 0, 1, big.data(), 10, link_of( 100 ), out );
    FACT_CHECK( out.numbers() == Numbers( { 0 } ), "what was not sent used no number: the next is numbered 0" );
  }

  /* (from the review of 2026-11-15: breaks these checks did not notice) */
  {
    Sending s( false );
    Sent out;
    s.send( 5, 0x1234, big.data(), 1000, link_of( 213, 0.004 ), out ); /* nothing set: left to decide, and at 0.4% it adds parity */
    FACT_CHECK( out.payloads.size() == 7, "nothing said of parity: it decides for itself (seven fragments at 0.4% loss)" );
    bool all = true;
    for ( size_t i = 0; i < out.payloads.size(); i++ ) {
      uint8_t type = 0, forward = 0;
      uint16_t id = 0;
      uint64_t seq = 0;
      std::string data;
      all = all && Gosh::decode_frame( out.payloads[i], type, forward, id, seq, data ) && forward == 5 && id == 0x1234;
    }
    FACT_CHECK( all, "every fragment is framed for its forward and conversation" );
    const std::string huge( 22601, 'y' );
    Sending t( false );
    Sent none;
    t.set_parity_mode( Sending::PARITY_NEVER );
    t.send( 0, 1, huge.data(), huge.size(), link_of( 13 + 4 + 100 ), none );
    FACT_CHECK( ( t.get_dropped_oversize() == 1 && t.get_fragmented() == 0 ), "a datagram dropped for its size is not counted as one sent in fragments" );
  }
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Sending );
}

FACTS_MAIN
