/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/FWD-2: UDP forwards' conversations (conversations.h), by themselves, with numbers for
 * time and a recorder for everything asked. No socket, no clock, no sleep.
 *
 * The fork's test, udpforward-test.cc, drives the same logic through its edge on real sockets, where time
 * is the real clock: it cannot wait two minutes for a conversation to be forgotten, refuse a
 * socket, or lose the one fragment it chooses. These are those cases.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/udp/conversations.h"
#include "vendor/facts/facts.h"



/* What an end is asked to do, written down. */
struct Recorder
{
  std::vector<std::string> sent;                                /* payloads for the side channel */
  std::vector<uint32_t> opened, closed;                         /* handles */
  std::vector<uint16_t> opened_ids;
  std::vector<std::pair<uint32_t, std::string> > to_targets;    /* handle, datagram */
  std::vector<std::pair<std::string, std::string> > to_applications; /* address, datagram */
  bool refuse; /* open(): there is no socket to be had */

  Recorder() : sent(), opened(), closed(), opened_ids(), to_targets(), to_applications(), refuse( false ) {}

  void send( const std::string& payload ) { sent.push_back( payload ); }
  bool open( uint32_t handle, uint8_t, uint16_t id )
  {
    if ( refuse ) {
      return false;
    }
    opened.push_back( handle );
    opened_ids.push_back( id );
    return true;
  }
  void close( uint32_t handle ) { closed.push_back( handle ); }
  void to_target( uint32_t handle, const std::string& datagram ) { to_targets.push_back( std::make_pair( handle, datagram ) ); }
  void to_application( uint8_t, const std::string& peer, const std::string& datagram )
  {
    to_applications.push_back( std::make_pair( peer, datagram ) );
  }
};

typedef Gosh::Conversations<Recorder> Conversations;

static Gosh::DatagramLink link_of( size_t max_payload, double loss = 0 )
{
  Gosh::DatagramLink link;
  link.ready = true;
  link.max_payload = max_payload;
  link.loss = loss;
  return link;
}

/* Two ends of the forwards of one direction. End a listens; end b sends to the targets. */
struct Pair
{
  Conversations a, b;
  Recorder ra, rb;
  Gosh::DatagramLink link;

  explicit Pair( size_t max_payload = 1200, bool reverse = false )
    : a( true, reverse, 2 ), b( false, reverse, 2 ), ra(), rb(), link( link_of( max_payload ) )
  {}

  /* Carries what a has sent to b; returns how many payloads. */
  size_t a_to_b( uint64_t now )
  {
    std::vector<std::string> on_their_way;
    on_their_way.swap( ra.sent );
    for ( size_t i = 0; i < on_their_way.size(); i++ ) {
      b.payload( on_their_way[i], now, rb );
    }
    return on_their_way.size();
  }
  size_t b_to_a( uint64_t now )
  {
    std::vector<std::string> on_their_way;
    on_their_way.swap( rb.sent );
    for ( size_t i = 0; i < on_their_way.size(); i++ ) {
      a.payload( on_their_way[i], now, ra );
    }
    return on_their_way.size();
  }
  void say( const std::string& peer, const std::string& datagram, uint64_t now, uint8_t forward = 0 )
  {
    a.from_application( forward, peer, datagram.data(), datagram.size(), now, link, ra );
  }
};

static std::string pattern( size_t length )
{
  std::string s( length, 0 );
  for ( size_t i = 0; i < length; i++ ) {
    s[i] = static_cast<char>( ( i * 2654435761u ) >> 11 );
  }
  return s;
}
FACTS( ThereAndBack )
{
  Pair p;
  p.say( "alice", "hello", 1000 );
  FACT_CHECK( ( p.ra.sent.size() == 1 && p.ra.sent[0].size() == 18 ), "an application's datagram goes as one payload of 13 + 5 bytes" );
  p.a_to_b( 1000 );
  FACT_CHECK( ( p.rb.opened.size() == 1 && p.rb.to_targets.size() == 1 && p.rb.to_targets[0].first == p.rb.opened[0]
           && p.rb.to_targets[0].second == "hello" ), "the other end opens one socket for the conversation, and sends the datagram on it" );
  p.b.from_target( p.rb.opened[0], "world", 5, 1001, p.link, p.rb );
  p.b_to_a( 1001 );
  FACT_CHECK( ( p.ra.to_applications.size() == 1 && p.ra.to_applications[0].first == "alice"
           && p.ra.to_applications[0].second == "world" ), "the target's answer comes back to the application that began it" );

  p.say( "alice", "again", 1002 );
  p.say( "bob", "me too", 1002 );
  p.say( "alice", "other forward", 1002, 1 );
  p.a_to_b( 1002 );
  FACT_CHECK( ( p.a.flow_count() == 3 && p.rb.opened.size() == 3 && p.rb.to_targets.size() == 4
           && p.rb.to_targets[1].first == p.rb.opened[0] && p.rb.to_targets[2].first == p.rb.opened[1] ), "the same application again is the same conversation; another, or another forward, is a new one" );
  FACT_CHECK( ( p.rb.opened_ids.size() == 3 && p.rb.opened_ids[0] != p.rb.opened_ids[1] ), "conversations on one forward have different numbers; another forward may reuse one" );
}
FACTS( WhichConversation )
{
  Pair p;
  p.say( "alice", "a", 1000 );
  p.say( "bob", "b", 1000 );
  p.a_to_b( 1000 );
  FACT_CHECK( ( p.rb.opened.size() == 2 && p.rb.opened[0] != p.rb.opened[1] ), "two conversations have two sockets, with different handles" );
  p.b.from_target( p.rb.opened[1], "for bob", 7, 1001, p.link, p.rb );
  p.b_to_a( 1001 );
  FACT_CHECK( ( p.ra.to_applications.size() == 1 && p.ra.to_applications[0].first == "bob" ), "an answer on the second socket goes to the second application" );

  /* The same number on two forwards is two conversations (the other end chooses the numbers). */
  Pair q;
  q.b.payload( Gosh::encode_frame( Gosh::TYPE_UDP_DATA, 0, 5, 1, "zero" ), 1000, q.rb );
  q.b.payload( Gosh::encode_frame( Gosh::TYPE_UDP_DATA, 1, 5, 2, "one" ), 1000, q.rb );
  FACT_CHECK( ( q.b.flow_count() == 2 && q.rb.opened.size() == 2 ), "the same number on two forwards is two conversations" );
  q.b.from_target( q.rb.opened[1], "back", 4, 1000, q.link, q.rb );
  uint8_t type = 0, forward = 0;
  uint16_t id = 0;
  uint64_t seq = 0;
  std::string data;
  FACT_CHECK( ( q.rb.sent.size() == 1 && Gosh::decode_frame( q.rb.sent[0], type, forward, id, seq, data ) && forward == 1
           && id == 5 && data == "back" ), "and an answer on the second's socket is framed for the second forward" );

  /* Forwards the server listens for have kinds of their own, there and back. */
  Pair r( 1200, true );
  r.say( "alice", "reverse", 1000 );
  FACT_CHECK( ( r.ra.sent.size() == 1 && Gosh::decode_frame( r.ra.sent[0], type, forward, id, seq, data )
           && type == Gosh::TYPE_UDP_REVERSE_DATA ), "a forward the server listens for sends its own kind of payload" );
  r.a_to_b( 1000 );
  FACT_CHECK( ( r.rb.to_targets.size() == 1 && r.rb.to_targets[0].second == "reverse" ), "which its other end takes" );
  r.b.payload( Gosh::encode_frame( Gosh::TYPE_UDP_DATA, 0, 9, 77, "plain" ), 1000, r.rb );
  FACT_CHECK( r.rb.to_targets.size() == 1, "and the plain kind it does not" );
}
FACTS( RefusedPayloads )
{
  Pair p;
  p.say( "alice", "once", 1000 );
  const std::string payload = p.ra.sent[0];
  p.a_to_b( 1000 );
  p.b.payload( payload, 1000, p.rb );
  FACT_CHECK( p.rb.to_targets.size() == 1, "REQ1/FWD-2: a payload that arrives twice is delivered once" );

  p.b.payload( Gosh::encode_frame( Gosh::TYPE_UDP_DATA, 2, 0, 50, "x" ), 1000, p.rb );
  FACT_CHECK( ( p.rb.to_targets.size() == 1 && p.b.flow_count() == 1 ), "a payload for a forward there is not is dropped" );
  p.b.payload( Gosh::encode_frame( Gosh::TYPE_UDP_REVERSE_DATA, 0, 0, 51, "x" ), 1000, p.rb );
  p.b.payload( Gosh::encode_frame( Gosh::TYPE_TCP_SEGMENT, 0, 0, 52, "x" ), 1000, p.rb );
  FACT_CHECK( p.rb.to_targets.size() == 1, "a payload of the other direction's kind, or of TCP's, is dropped" );
  p.b.payload( payload.substr( 0, 12 ), 1000, p.rb );
  FACT_CHECK( p.rb.to_targets.size() == 1, "a payload shorter than the header is dropped" );

  p.a.payload( Gosh::encode_frame( Gosh::TYPE_UDP_DATA, 0, 999, 60, "reply" ), 1000, p.ra );
  FACT_CHECK( p.ra.to_applications.empty(), "an answer for a conversation the listening end does not have is dropped" );

  p.rb.refuse = true;
  p.b.payload( Gosh::encode_frame( Gosh::TYPE_UDP_DATA, 1, 7, 61, "no socket" ), 1000, p.rb );
  FACT_CHECK( ( p.rb.to_targets.size() == 1 && p.b.flow_count() == 1 ), "no socket to be had for a new conversation: the datagram is dropped, and no conversation kept" );

  Gosh::DatagramLink down = p.link;
  down.ready = false;
  p.ra.sent.clear();
  p.a.from_application( 0, "alice", "lost", 4, 1000, down, p.ra );
  FACT_CHECK( p.ra.sent.empty(), "the session not ready: an application's datagram is dropped, not kept" );
}
FACTS( Forgotten )
{
  Pair p;
  p.say( "alice", "a", 1000 );
  p.a_to_b( 1000 );
  p.say( "bob", "b", 1000 + 120000 ); /* two minutes later, to the millisecond: alice is not forgotten yet */
  p.a_to_b( 1000 + 120000 );
  FACT_CHECK( ( p.a.flow_count() == 2 && p.b.flow_count() == 2 && p.rb.closed.empty() ), "a conversation quiet for exactly two minutes is still there when another begins" );
  p.say( "carol", "c", 1000 + 120001 ); /* a millisecond more */
  p.a_to_b( 1000 + 120001 );
  FACT_CHECK( ( p.a.flow_count() == 2 && p.b.flow_count() == 2 && p.rb.closed.size() == 1 && p.rb.closed[0] == p.rb.opened[0] ), "a millisecond later it is forgotten at both ends, and its socket closed" );

  /* Use keeps a conversation: used again after a minute, it is there two minutes and a millisecond after it began. */
  Pair u;
  u.say( "alice", "a", 1000 );
  u.say( "alice", "a", 61000 );
  u.say( "bob", "b", 1000 + 120001 );
  FACT_CHECK( u.a.flow_count() == 2, "a conversation used again a minute on is not forgotten a minute after that" );

  /* So do answers, at both ends: a conversation where only the target speaks is not forgotten. */
  Pair v;
  v.say( "alice", "a", 1000 );
  v.a_to_b( 1000 );
  v.b.from_target( v.rb.opened[0], "tick", 4, 100000, v.link, v.rb );
  v.b_to_a( 100000 );
  v.say( "bob", "b", 1000 + 120001 );
  v.a_to_b( 1000 + 120001 );
  FACT_CHECK( ( v.a.flow_count() == 2 && v.b.flow_count() == 2 && v.rb.closed.empty() ), "a conversation in which only the target has spoken since is kept, at both ends" );
  v.b.from_target( v.rb.opened[0], "tock", 4, 1000 + 120002, v.link, v.rb );
  v.b_to_a( 1000 + 120002 );
  FACT_CHECK( ( v.ra.to_applications.size() == 2 && v.ra.to_applications[1].first == "alice" ), "and its answers still reach the application" );

  /* Sixty-four at once, each used at a different time; the sixty-fifth pushes out the quietest. */
  Pair q;
  for ( int i = 0; i < 64; i++ ) {
    q.say( "peer" + std::to_string( i ), "x", 5000 + i );
    q.a_to_b( 5000 + i );
  }
  q.say( "peer0", "x", 5100 ); /* the first is used again, so the second is now the quietest */
  q.a_to_b( 5100 );
  FACT_CHECK( ( q.a.flow_count() == 64 && q.b.flow_count() == 64 && q.rb.closed.empty() ), "(sixty-four conversations)" );
  q.say( "one more", "x", 5200 );
  q.a_to_b( 5200 );
  FACT_CHECK( ( q.a.flow_count() == 64 && q.b.flow_count() == 64 && q.rb.closed.size() == 1 && q.rb.closed[0] == q.rb.opened[1] ), "the sixty-fifth conversation pushes out the one quiet longest: the second, not the first" );
}
FACTS( Fragments )
{
  /* 213 bytes a payload: 200 for a fragment, 196 of them the datagram's. A datagram of 1,000 bytes is 6 pieces. */
  Pair p( 213 );
  const std::string big = pattern( 1000 );

  p.a.set_parity_mode( Conversations::PARITY_NEVER );
  p.say( "alice", big, 1000 );
  FACT_CHECK( ( p.ra.sent.size() == 6 && p.a.get_fragmented() == 1 && p.a.get_with_parity() == 0 ), "1,000 bytes where a payload holds 213: six fragments, no parity" );
  p.a_to_b( 1000 );
  FACT_CHECK( ( p.rb.to_targets.size() == 1 && p.rb.to_targets[0].second == big && p.b.get_reassembled() == 1 ), "and the datagram is rebuilt whole at the other end" );

  /* One fragment lost, no parity: nothing is delivered, and a second after its first fragment it is abandoned. */
  p.say( "alice", big, 2000 );
  p.ra.sent.erase( p.ra.sent.begin() + 2 );
  p.a_to_b( 2000 );
  FACT_CHECK( ( p.rb.to_targets.size() == 1 && p.b.partial_count() == 1 ), "a fragment lost with no parity: nothing delivered, the rest kept waiting" );
  p.a.set_parity_mode( Conversations::PARITY_ALWAYS );
  p.say( "alice", big, 3000 ); /* exactly a second later: still waiting */
  const size_t with_parity = p.ra.sent.size();
  p.ra.sent.erase( p.ra.sent.begin() + 2 );
  p.a_to_b( 3000 );
  FACT_CHECK( ( with_parity == 7 && p.rb.to_targets.size() == 2 && p.rb.to_targets[1].second == big && p.b.get_recovered() == 1 ), "with parity: seven fragments; one lost, and the datagram is rebuilt from the rest" );
  FACT_CHECK( ( p.b.partial_count() == 1 && p.b.get_abandoned() == 0 ), "exactly a second on, the earlier datagram is still waited for" );
  p.say( "alice", big, 3001 );
  p.a_to_b( 3001 );
  FACT_CHECK( ( p.b.partial_count() == 0 && p.b.get_abandoned() == 1 ), "a millisecond later it is abandoned" );

  /* A fragment that arrives twice, and one that claims a datagram of another conversation. */
  Pair f( 213 );
  f.a.set_parity_mode( Conversations::PARITY_NEVER );
  f.say( "alice", big, 1000 );
  std::vector<std::string> six = f.ra.sent;
  f.ra.sent.clear();
  for ( int i = 0; i < 5; i++ ) {
    f.b.payload( six[i], 1000, f.rb );
  }
  uint8_t type = 0, forward = 0;
  uint16_t id = 0;
  uint64_t seq = 0;
  std::string piece;
  Gosh::decode_frame( six[5], type, forward, id, seq, piece );
  f.b.payload( Gosh::encode_frame( type, forward, id + 1, seq, piece ), 1000, f.rb );
  FACT_CHECK( ( f.rb.to_targets.empty() && f.b.partial_count() == 1 ), "the last fragment, but said to be of another conversation: not taken, and the datagram not rebuilt" );
  f.b.payload( six[5], 1000, f.rb );
  FACT_CHECK( f.rb.to_targets.empty(), "(that used its number: the true one after it is a replay, and refused)" );

  /* Whether to add parity when left to decide: at the session's loss, would it be lost 2 times in 100 without? */
  Pair q( 213 );
  q.link.loss = 0.003; /* six fragments: lost 1.8 times in 100 */
  q.say( "alice", big, 1000 );
  FACT_CHECK( q.ra.sent.size() == 6, "left to decide, at 0.3% loss six fragments go without parity" );
  q.ra.sent.clear();
  q.link.loss = 0.004; /* 2.4 times in 100 */
  q.say( "alice", big, 1000 );
  FACT_CHECK( q.ra.sent.size() == 7, "and at 0.4% with it" );

  /* Too large: 255 fragments at most, parity among them, which leaves 226 pieces. Here a piece is 100 bytes. */
  Pair r( 13 + 4 + 100 );
  r.a.set_parity_mode( Conversations::PARITY_NEVER );
  r.say( "alice", pattern( 22601 ), 1000 );
  FACT_CHECK( ( r.ra.sent.empty() && r.a.get_dropped_oversize() == 1 ), "a datagram needing more than 226 pieces is dropped, and counted" );
  r.say( "alice", pattern( 22600 ), 1000 );
  FACT_CHECK( r.ra.sent.size() == 226, "one needing exactly 226 is sent" );
}
/* REQ1/FWD-2: UDP forwards' conversations, with no socket and no clock */
FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( ThereAndBack );
  FACTS_REGISTER( WhichConversation );
  FACTS_REGISTER( RefusedPayloads );
  FACTS_REGISTER( Forgotten );
  FACTS_REGISTER( Fragments );
}

FACTS_MAIN
