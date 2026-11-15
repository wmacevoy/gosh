/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/FWD-1, REQ1/FWD-3: the connection logic (connection.h), by itself, with numbers for time
 * and a recorder for everything it asks to have done. No socket, no clock, no sleep.
 *
 * connection-diff.cc holds this logic to the fork's adapter over thousands of scripted
 * scenarios. These are the cases that test cannot reach, because the fork's adapter needs real
 * sockets and a real socket pair is never slow, never refuses and never runs out:
 *
 *   an application that stops taking bytes (flow control, the window probe, the window update),
 *   and the end of a connection waiting on it;
 *   two connections at once;
 *   a target that refuses, and one that never answers;
 *   the sixty-fifth connection;
 *   a session not ready to carry anything;
 *   six hours out of contact with data on its way (REQ1/FWD-3), against the same silence while
 *   in contact, which does end a connection.
 *
 * And REQ14 (Selective acknowledgment; queue item 17): segments lost on the way, with numbers
 * for how much is then sent again; an end that does not know selective acknowledgment (as pf.7
 * does not) is never sent it; and an end that says it holds what it does not.
 */

#include <cstdio>
#include <exception>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "src/forwarding/tcp/connection.h"

using Network::TCPInUDP;

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-78s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

/* What an end is asked to do, written down; and what it is told back, as the test sets it. */
struct Recorder
{
  std::vector<std::pair<uint8_t, TCPInUDP> > sent;
  uint32_t sequence; /* the next first sequence number to hand out */

  bool has_target; /* connect(): whether there is anything to reach */
  int reached;     /* connecting(): 0 not yet, 1 reached, -1 refused */
  unsigned int connects;

  long takes;      /* deliver(): how many more bytes the application will take; -1 for all it is given */
  bool gone;       /* deliver(): the application has gone */
  std::string got; /* what the application has been given, on any connection */
  std::map<uint32_t, std::string> got_on; /* and on each */
  bool shut;       /* shut_write() was called */
  std::vector<std::pair<uint32_t, bool> > closed; /* close(): the id, and whether with a reset */

  Recorder()
    : sent(), sequence( 1000 ), has_target( true ), reached( 1 ), connects( 0 ), takes( -1 ), gone( false ), got(),
      got_on(), shut( false ), closed()
  {}

  void send( uint8_t forward, const TCPInUDP& segment ) { sent.push_back( std::make_pair( forward, segment ) ); }
  uint32_t fresh_sequence( void ) { return sequence += 100000; }
  bool connect( uint32_t, uint8_t )
  {
    connects++;
    return has_target;
  }
  int connecting( uint32_t ) { return reached; }
  long deliver( uint32_t id, const char* data, size_t length )
  {
    if ( gone ) {
      return -1;
    }
    const size_t n = takes < 0 ? length : std::min<size_t>( length, takes );
    got.append( data, n );
    got_on[id].append( data, n );
    if ( takes >= 0 ) {
      takes -= n;
    }
    return n;
  }
  void shut_write( uint32_t ) { shut = true; }
  void close( uint32_t id, bool reset ) { closed.push_back( std::make_pair( id, reset ) ); }
};

typedef Gosh::Connections<Recorder> Connections;

/* Two ends and what lies between them. End 0 listens for an application; end 1 connects to the target. */
struct Pair
{
  Recorder out[2];
  Connections listening, connecting;
  Gosh::Link link;
  uint64_t now;
  bool carrying; /* whether segments get from one end to the other */
  bool backwards; /* each batch of segments arrives in the opposite order to that in which it was sent */
  bool twice;     /* and every segment arrives twice */
  TCPInUDP last_from[2]; /* the last segment each end sent that got there */

  /* What is lost on the way from end 0 to end 1, of the segments that carry data: */
  std::vector<unsigned int> lose; /* these, counted from 0 at the last reset_count(); each once */
  unsigned int lose_in_1000;      /* and this many in a thousand, by a seeded generator */
  uint32_t chance;
  unsigned int data_segments; /* how many segments with data end 0 has sent, */
  size_t data_bytes;          /* and how many bytes of data in them, since the last reset_count() */
  unsigned int exchanges;     /* how many times end 0's segments were carried over, since then */
  unsigned int said_held;     /* how many segments from end 1 said what it holds out of order */
  unsigned int options_after_syn[2]; /* how many segments from each end, not a SYN, had any option */
  /* An end 1 that lies: the last segment in "lose" is said, on every acknowledgment, to be held. */
  bool lying;
  bool lie_known;
  uint32_t lie_from, lie_to;
  int lie_from_shift, lie_to_shift; /* (the lie's range, moved by this much at each end) */
  unsigned int lose_acknowledgments; /* this many of end 1's next segments are lost, too */
  std::string last_said; /* the options on the last segment from end 1 that said what it holds */
  bool edge_known;
  uint32_t edge;       /* the furthest end 1 has said it will take: its acknowledgment and its window */
  size_t beyond_edge;  /* the furthest any data from end 0 reached past that, since the last reset_count() */

  explicit Pair( bool sack0 = true, bool sack1 = true )
    : listening( true, sack0 ), connecting( false, sack1 ), link(), now( 5000 ), carrying( true ), backwards( false ),
      twice( false ), last_from(), lose(),
      lose_in_1000( 0 ), chance( 2463534242u ), data_segments( 0 ), data_bytes( 0 ), exchanges( 0 ), said_held( 0 ), lying( false ),
      lie_known( false ), lie_from( 0 ), lie_to( 0 ), lie_from_shift( 0 ), lie_to_shift( 0 ), lose_acknowledgments( 0 ),
      last_said(), edge_known( false ), edge( 0 ), beyond_edge( 0 )
  {
    options_after_syn[0] = options_after_syn[1] = 0;
    link.ready = true;
    link.in_contact = true;
    link.max_datagram = 1187;
  }

  Connections& end( int e ) { return e == 0 ? listening : connecting; }

  void reset_count( void )
  {
    data_segments = 0;
    data_bytes = 0;
    exchanges = 0;
    beyond_edge = 0;
  }

  /* Whether this segment, on its way from end e, is lost. Counts it, and what it says, either way. */
  bool lost( int e, TCPInUDP& segment )
  {
    if ( !segment.has( TCPInUDP::SYN ) && !segment.options.empty() ) {
      options_after_syn[e]++;
      if ( e == 1 && segment.options.find( '\5' ) != std::string::npos ) {
        said_held++;
      }
    }
    if ( e == 1 && lying && lie_known && segment.payload.empty() && !segment.has( TCPInUDP::SYN ) ) {
      const char said[12] = { 1, 1, 5, 10,
                              static_cast<char>( lie_from >> 24 ), static_cast<char>( lie_from >> 16 ),
                              static_cast<char>( lie_from >> 8 ), static_cast<char>( lie_from ),
                              static_cast<char>( lie_to >> 24 ), static_cast<char>( lie_to >> 16 ),
                              static_cast<char>( lie_to >> 8 ), static_cast<char>( lie_to ) };
      segment.options.assign( said, sizeof( said ) );
    }
    if ( e == 1 && !segment.has( TCPInUDP::SYN ) ) {
      if ( lose_acknowledgments ) {
        lose_acknowledgments--;
        return true;
      }
      if ( segment.options.find( '\5' ) != std::string::npos ) {
        last_said = segment.options;
      }
      /* (both ends here shift the window by five: the logic's WINDOW_SHIFT) */
      const uint32_t now_edge = segment.ack + ( static_cast<uint32_t>( segment.window ) << 5 );
      if ( !edge_known || static_cast<int32_t>( now_edge - edge ) > 0 ) {
        edge = now_edge;
        edge_known = true;
      }
    }
    if ( e != 0 || segment.payload.empty() ) {
      return false;
    }
    if ( edge_known ) {
      const int32_t past = static_cast<int32_t>( segment.seq + segment.payload.size() - edge );
      if ( past > 0 && static_cast<size_t>( past ) > beyond_edge ) {
        beyond_edge = past;
      }
    }
    const unsigned int which = data_segments++;
    data_bytes += segment.payload.size();
    bool gone = false;
    for ( size_t i = 0; i < lose.size(); i++ ) {
      if ( lose[i] == which ) {
        gone = true;
      }
    }
    if ( lose_in_1000 ) {
      chance = chance * 1664525 + 1013904223;
      gone = gone || ( chance >> 10 ) % 1000 < lose_in_1000;
    }
    if ( gone && lying && !lie_known && !lose.empty() && which == lose.back() ) {
      lie_known = true;
      lie_from = segment.seq + lie_from_shift;
      lie_to = segment.seq + segment.payload.size() + lie_to_shift;
    }
    return gone;
  }

  /* Carries everything each end has sent to the other, and what that provokes, until both are quiet. */
  void carry( void )
  {
    for ( int rounds = 0; rounds < 100000; rounds++ ) {
      if ( out[0].sent.empty() && out[1].sent.empty() ) {
        return;
      }
      for ( int e = 0; e < 2; e++ ) {
        std::vector<std::pair<uint8_t, TCPInUDP> > on_their_way;
        on_their_way.swap( out[e].sent );
        if ( e == 0 && carrying && !on_their_way.empty() ) {
          exchanges++;
        }
        for ( size_t k = 0; carrying && k < on_their_way.size(); k++ ) {
          const size_t i = backwards ? on_their_way.size() - 1 - k : k;
          if ( lost( e, on_their_way[i].second ) ) {
            continue;
          }
          last_from[e] = on_their_way[i].second;
          for ( int copy = 0; copy < ( twice ? 2 : 1 ); copy++ ) {
            end( 1 - e ).segment( on_their_way[i].first, on_their_way[i].second, now, link, out[1 - e] );
          }
        }
      }
    }
    check( "(two ends fall quiet)", false );
  }

  void tick( uint64_t later )
  {
    now += later;
    listening.tick( now, link, out[0] );
    connecting.tick( now, link, out[1] );
  }

  /* An application connects at the listening end, and the handshake runs. Returns the connection's id. */
  uint32_t open( void )
  {
    uint32_t id = 0;
    check( "(a connection opens)", listening.opened( 0, now, link, out[0], id ) );
    carry(); /* the SYN arrives; the target is asked for */
    tick( 1 ); /* the target is found to be reached */
    carry();
    return id;
  }

  /* The application at end e writes these bytes, as fast as its end will read them. */
  size_t write( int e, uint32_t id, const std::string& data )
  {
    size_t written = 0;
    while ( written < data.size() ) {
      const size_t want = end( e ).wants( id );
      if ( !want ) {
        break;
      }
      const size_t n = std::min( want, data.size() - written );
      end( e ).read( id, data.data() + written, n, now, link, out[e] );
      written += n;
    }
    return written;
  }
};

static std::string pattern( size_t length )
{
  std::string s( length, 0 );
  for ( size_t i = 0; i < length; i++ ) {
    s[i] = static_cast<char>( ( i * 2654435761u ) >> 13 );
  }
  return s;
}

static void test_transfer( void )
{
  Pair p;
  const uint32_t id = p.open();
  const std::string there = pattern( 300000 ), back = pattern( 70001 );
  size_t a = 0, b = 0;
  for ( int i = 0; i < 2000 && ( a < there.size() || b < back.size() ); i++ ) {
    a += p.write( 0, id, there.substr( a ) );
    b += p.write( 1, id, back.substr( b ) );
    p.carry();
    p.tick( 1 );
  }
  p.carry();
  check( "bytes written at the listening end arrive whole at the target", p.out[1].got == there );
  check( "and bytes written at the target arrive whole at the application", p.out[0].got == back );

  p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] ); /* the application finishes writing */
  p.carry();
  check( "one end finishing is passed on, and the connection stays for the other", p.out[1].shut && p.out[1].closed.empty() );
  p.connecting.read( id, NULL, 0, p.now, p.link, p.out[1] );
  p.carry();
  check( "both finished: each end closes, in order and not with a reset",
         p.out[0].closed.size() == 1 && !p.out[0].closed[0].second && p.out[1].closed.size() == 1 && !p.out[1].closed[0].second
           && p.listening.count() == 0 && p.connecting.count() == 0 );
}

static void test_slow_application( void )
{
  Pair p;
  const uint32_t id = p.open();
  p.out[1].takes = 0; /* the target takes nothing */

  const std::string data = pattern( 3 * 1024 * 1024 );
  size_t written = 0;
  for ( int i = 0; i < 4000; i++ ) {
    written += p.write( 0, id, data.substr( written ) );
    p.carry();
    p.tick( 50 );
  }
  p.carry();
  check( "an application that takes nothing is given nothing", p.out[1].got.empty() );
  check( "and the sender stops: it has read no more than its two buffers hold",
         written >= 1024 * 1024 && written <= 2 * 1024 * 1024 ); /* (1 MB each way: Operating envelope) */
  check( "neither end has given up", p.out[0].closed.empty() && p.out[1].closed.empty() );

  /* With the window shut and nothing on its way, the sender probes it: one byte, so as not to wait for ever. */
  p.out[0].sent.clear();
  p.carrying = false;
  for ( int i = 0; i < 100 && p.out[0].sent.empty(); i++ ) {
    p.tick( 1000 );
  }
  check( "with the window shut, the sender probes it with one byte",
         !p.out[0].sent.empty() && p.out[0].sent[0].second.payload.size() == 1 );
  p.carrying = true;
  p.carry();

  /* The application takes a little: enough room for two segments and not for three. */
  const size_t mss = 1187 - TCPInUDP::DATAGRAM_HEADER_LEN;
  p.out[1].takes = 2 * mss + mss / 2;
  p.out[1].sent.clear();
  p.now += 1;
  p.connecting.tick( p.now, p.link, p.out[1] );
  check( "when there is room for two segments again, the receiver says so unasked",
         p.out[1].sent.size() == 1 && p.out[1].sent[0].second.payload.empty() && p.out[1].sent[0].second.window > 0 );

  /* And then everything. */
  p.out[1].takes = -1;
  for ( int i = 0; i < 20000 && ( written < data.size() || p.out[1].got.size() < data.size() ); i++ ) {
    written += p.write( 0, id, data.substr( written ) );
    p.carry();
    p.tick( 5 );
  }
  check( "once the application takes bytes again, all of them arrive, in order", p.out[1].got == data );
}

/* More than 64 KB can be on its way at once: the windows are scaled. */
static void test_window( void )
{
  Pair p;
  const uint32_t id = p.open();
  p.out[1].takes = 0;
  const std::string data = pattern( 3 * 1024 * 1024 );
  size_t written = 0;
  for ( int i = 0; i < 4000; i++ ) {
    written += p.write( 0, id, data.substr( written ) );
    p.carry();
    p.tick( 50 );
  }
  p.out[1].takes = -1;
  p.now += 1;
  p.connecting.tick( p.now, p.link, p.out[1] ); /* the application takes what its end is holding, and nothing more arrives */
  check( "a megabyte was waiting at the receiver: its window was not held to what 16 bits can say",
         p.out[1].got.size() > 1000000 && p.out[1].got.size() <= 1024 * 1024 && p.out[1].got == data.substr( 0, p.out[1].got.size() ) );

  /* And the sender believes it: once it has learnt the path is good, it puts far more than 64 KB on its way at once. */
  Pair q;
  const uint32_t id2 = q.open();
  size_t sent = 0;
  for ( int i = 0; i < 3000 && q.out[1].got.size() < 2000000; i++ ) {
    sent += q.write( 0, id2, data.substr( sent, 2000000 - sent ) );
    q.carry();
    q.tick( 1 );
  }
  q.out[0].sent.clear();
  q.carrying = false; /* nothing is acknowledged from here on */
  q.write( 0, id2, data.substr( 0, 900000 ) );
  size_t on_its_way = 0;
  for ( size_t i = 0; i < q.out[0].sent.size(); i++ ) {
    on_its_way += q.out[0].sent[i].second.payload.size();
  }
  check( "with nothing yet acknowledged, the sender has several hundred kilobytes on their way at once",
         q.out[1].got.size() == 2000000 && on_its_way > 300000 );
}

/* A handshake that is never answered, while the session is in contact, is given up. */
static void test_handshake_gives_up( void )
{
  {
    Pair p;
    uint32_t id = 0;
    p.carrying = false;
    p.listening.opened( 0, p.now, p.link, p.out[0], id );
    size_t syns = p.out[0].sent.size();
    p.out[0].sent.clear();
    for ( int i = 0; i < 400 && p.out[0].closed.empty(); i++ ) {
      p.tick( 1000 );
      syns += p.out[0].sent.size();
      p.out[0].sent.clear();
    }
    check( "a SYN never answered is sent six times in all, and then the application is refused",
           syns == 6 && p.out[0].closed.size() == 1 && p.out[0].closed[0].second && p.listening.count() == 0 );
  }
  {
    Pair p;
    uint32_t id = 0;
    p.listening.opened( 0, p.now, p.link, p.out[0], id );
    p.carry();       /* the SYN arrives */
    p.carrying = false;
    p.tick( 1 );     /* the target is reached, and the acceptance is sent into nothing */
    bool told = false;
    for ( int i = 0; i < 400 && p.out[1].closed.empty(); i++ ) {
      p.out[1].sent.clear();
      p.tick( 1000 );
    }
    for ( size_t i = 0; i < p.out[1].sent.size(); i++ ) {
      told = told || p.out[1].sent[i].second.has( TCPInUDP::RST );
    }
    check( "an acceptance never acknowledged: the target's socket is closed with a reset, and the other end is told",
           p.out[1].closed.size() == 1 && p.out[1].closed[0].second && told && p.connecting.count() == 0 );
  }
}

/* The end of a connection waits for an application that is slow to take what came before it. */
static void test_slow_finish( void )
{
  Pair p;
  const uint32_t id = p.open();
  p.out[1].takes = 0; /* the target takes nothing yet */
  const std::string data = pattern( 5000 );
  p.write( 0, id, data );
  p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] ); /* and the application has finished writing */
  p.carry();
  check( "the end has arrived, but bytes before it are undelivered: the target is not yet told it is over",
         p.out[1].got.empty() && !p.out[1].shut && p.out[1].closed.empty() );
  check( "while an application is slow, its end asks to be woken in ten milliseconds to try again",
         p.connecting.wait_time( p.now ) == 10 );

  p.connecting.read( id, NULL, 0, p.now, p.link, p.out[1] ); /* the target finishes writing too */
  p.carry();
  check( "both ends have finished, and bytes are still undelivered: the connection is not closed",
         p.out[1].closed.empty() && p.connecting.count() == 1 );

  p.out[1].takes = 100;
  p.tick( 10 );
  check( "it takes a little: still not told it is over", p.out[1].got.size() == 100 && !p.out[1].shut && p.out[1].closed.empty() );
  p.out[1].takes = -1;
  p.tick( 10 );
  p.carry();
  check( "once it has taken everything, it is told, and only then is the connection closed, in order",
         p.out[1].got == data && p.out[1].shut && p.out[1].closed.size() == 1 && !p.out[1].closed[0].second );
}

/*
 * Queue item 18. One end has closed in order and forgotten the connection; the other still
 * holds bytes its slow application has not taken. Anything that end now sends (it says its
 * window has opened as the application takes bytes; or a segment of its arrives late, or twice)
 * is answered with a reset by the end that has forgotten. Everything from that end had already
 * arrived, in order and complete, its finish included: the reset has nothing to say about
 * those bytes, and they are delivered.
 */
static void test_reset_after_everything_arrived( void )
{
  /* The receive buffer exactly full (1 MB), so that taking bytes opens a closed window, which is said. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.out[1].takes = 0;
    const std::string data = pattern( 1024 * 1024 );
    size_t written = 0;
    for ( int i = 0; i < 4000 && written < data.size(); i++ ) {
      written += p.write( 0, id, data.substr( written ) );
      p.carry();
      p.tick( 1 );
    }
    p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] );
    p.carry();
    p.connecting.read( id, NULL, 0, p.now, p.link, p.out[1] );
    p.carry();
    check( "(1 MB sent and held; both ends finished; the sending end has closed, in order, and forgotten)",
           written == data.size() && p.out[0].closed.size() == 1 && !p.out[0].closed[0].second && p.listening.count() == 0
             && p.out[1].got.empty() );
    p.out[1].takes = 100000; /* the application takes some: the window opens, and that is said */
    p.tick( 10 );
    p.carry();
    unsigned int said_after = 0;
    for ( int i = 0; i < 8; i++ ) {
      p.out[1].takes = 100000;
      p.tick( 10 );
      said_after += p.out[1].sent.size();
      p.carry();
    }
    p.out[1].takes = -1;
    p.tick( 10 );
    said_after += p.out[1].sent.size();
    p.carry();
    check( "told once that the other end has gone, it sends that end nothing more", said_after == 0 );
    check( "the slow application takes bytes after the other end has gone: it gets every one of the 1 MB", p.out[1].got == data );
    check( "and is then told it is over, and the connection closes in order, not with a reset",
           p.out[1].shut && p.out[1].closed.size() == 1 && !p.out[1].closed[0].second && p.connecting.count() == 0 );
  }

  /* A segment that arrives a second time, late, at the end that has forgotten. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.out[1].takes = 0;
    const std::string data = pattern( 5000 );
    p.write( 0, id, data );
    p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] );
    p.carry();
    p.connecting.read( id, NULL, 0, p.now, p.link, p.out[1] );
    p.carry();
    check( "(5,000 bytes held; the sending end has closed and forgotten)", p.listening.count() == 0 && p.out[1].got.empty() );
    p.listening.segment( 0, p.last_from[1], p.now, p.link, p.out[0] ); /* the last thing it sent, again */
    check( "(the end that has forgotten answers it with a reset)",
           p.out[0].sent.size() == 1 && p.out[0].sent[0].second.has( TCPInUDP::RST ) );
    p.carry();
    p.out[1].takes = -1;
    p.tick( 10 );
    p.carry();
    check( "a late copy of a segment, answered with a reset: the 5,000 bytes are delivered all the same",
           p.out[1].got == data && p.out[1].shut && p.out[1].closed.size() == 1 && !p.out[1].closed[0].second );
  }

  /* The other end finished and then forgot us before it had all of ours: what we hold is delivered, then a reset. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.out[1].takes = 0;
    const std::string data = pattern( 5000 );
    p.write( 0, id, data );
    p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] );
    p.carry();
    p.carrying = false;
    p.write( 1, id, pattern( 3000 ) ); /* the target's answer, which never gets there */
    p.out[1].sent.clear();
    p.carrying = true;
    TCPInUDP reset = p.last_from[0];
    reset.flags = TCPInUDP::RST;
    reset.seq = reset.seq + reset.payload.size() + 1; /* (after its finish) */
    reset.payload.clear();
    p.connecting.segment( 0, reset, p.now, p.link, p.out[1] );
    check( "a reset after its finish, with our answer not yet acknowledged: nothing is closed while bytes are held",
           p.out[1].closed.empty() && p.out[1].got.empty() );

    /* Something the other end sent before it forgot arrives after its reset: its finish, again. */
    bool threw = false;
    try {
      p.connecting.segment( 0, p.last_from[0], p.now, p.link, p.out[1] );
    } catch ( const std::exception& e ) {
      threw = true;
    }
    check( "a segment of its that arrives after its reset changes nothing, and breaks nothing",
           !threw && p.out[1].closed.empty() && p.connecting.count() == 1 );

    /* The application has more to say. It is taken and goes nowhere, so that the application is not left stuck writing. */
    check( "the application's further bytes are still taken from it", p.connecting.wants( id ) > 0 );
    p.connecting.read( id, "more", 4, p.now, p.link, p.out[1] );
    check( "and nothing is sent to the end that has gone", p.out[1].sent.empty() && p.connecting.count() == 1 );
    const std::string much( 65536, 'w' );
    size_t taken = 0;
    for ( int i = 0; i < 48 && p.connecting.wants( id ); i++ ) { /* 3 MB: three times what a send buffer holds */
      p.connecting.read( id, much.data(), std::min( much.size(), p.connecting.wants( id ) ), p.now, p.link, p.out[1] );
      taken += much.size();
    }
    check( "however much it writes: 3 MB is taken, where a buffer of what could not be sent would have filled at 1 MB",
           taken == 48 * much.size() && p.connecting.wants( id ) > 0 && p.out[1].sent.empty() );

    p.out[1].takes = -1;
    p.tick( 10 );
    /*
     * In order, not with a reset: a reset given to the application's socket throws away what
     * it has not yet read, which is the loss this is here to prevent. Its unacknowledged answer
     * is lost either way; it finds out as with any TCP, when it next writes.
     */
    check( "the 5,000 bytes are delivered, and the connection then closes in order",
           p.out[1].got == data && p.out[1].shut && p.out[1].closed.size() == 1 && !p.out[1].closed[0].second );
  }

  /* Much of ours in flight; its reset after its finish; then an old acknowledgment of a little of ours turns up. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.out[1].takes = 0;
    const std::string data = pattern( 5000 );
    p.write( 0, id, data );
    p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] );
    p.carry();
    p.carrying = false;
    const std::string ours = pattern( 2 * 1024 * 1024 );
    size_t queued = 0;
    for ( int i = 0; i < 64 && p.connecting.wants( id ); i++ ) {
      queued += p.write( 1, id, ours.substr( queued, 65536 ) );
    }
    check( "(our send buffer is full: 1 MB waits, and no more is wanted from the application)",
           queued == 1024 * 1024 && p.connecting.wants( id ) == 0 );
    const uint32_t ours_begin = p.out[1].sent.empty() ? 0 : p.out[1].sent[0].second.seq;
    p.out[1].sent.clear();
    p.carrying = true;
    TCPInUDP reset = p.last_from[0];
    reset.flags = TCPInUDP::RST;
    reset.seq = reset.seq + reset.payload.size() + 1;
    reset.payload.clear();
    p.connecting.segment( 0, reset, p.now, p.link, p.out[1] );
    check( "the other end gone, what waited to be sent is let go, and the application is read from again",
           p.connecting.wants( id ) > 0 );
    TCPInUDP late = reset;
    late.flags = TCPInUDP::ACK;
    late.ack = ours_begin + 1000;
    late.window = 1;
    p.connecting.segment( 0, late, p.now, p.link, p.out[1] );
    unsigned int sent_after = p.out[1].sent.size();
    for ( int i = 0; i < 400; i++ ) { /* 400 s, the application reading nothing */
      p.tick( 1000 );
      sent_after += p.out[1].sent.size();
      p.out[1].sent.clear();
    }
    check( "an old acknowledgment after the reset starts no timer: 400 s on, the bytes are still held and nothing was sent",
           p.out[1].closed.empty() && p.connecting.count() == 1 && sent_after == 0 );
    p.out[1].takes = -1;
    p.tick( 10 );
    check( "and when the application reads, it gets them", p.out[1].got == data && !p.out[1].closed.empty() && !p.out[1].closed[0].second );
  }

  /* The other end had everything and closed; only its last acknowledgment, of our finish, was lost. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.out[1].takes = 0;
    const std::string data = pattern( 5000 );
    p.write( 0, id, data );
    p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] );
    p.carry();
    p.connecting.read( id, NULL, 0, p.now, p.link, p.out[1] ); /* our finish goes... */
    std::vector<std::pair<uint8_t, TCPInUDP> > ours;
    ours.swap( p.out[1].sent );
    for ( size_t i = 0; i < ours.size(); i++ ) {
      p.listening.segment( ours[i].first, ours[i].second, p.now, p.link, p.out[0] );
    }
    p.out[0].sent.clear(); /* ...and the acknowledgment of it is lost */
    check( "(the other end has everything, and has closed and forgotten; we do not know our finish arrived)",
           p.listening.count() == 0 && p.connecting.count() == 1 );
    for ( int i = 0; i < 5 && p.out[0].sent.empty(); i++ ) {
      p.tick( 1000 ); /* our finish is sent again, and answered with a reset */
      p.carry();
    }
    p.out[1].takes = -1;
    p.tick( 10 );
    p.carry();
    check( "only the last acknowledgment lost: the bytes are delivered and the connection closes in order",
           p.out[1].got == data && p.out[1].closed.size() == 1 && !p.out[1].closed[0].second );
  }

  /* A full buffer, and the reset drawn by a late copy: taking bytes then opens the window, of which nothing is said. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.out[1].takes = 0;
    const std::string data = pattern( 1024 * 1024 );
    size_t written = 0;
    for ( int i = 0; i < 4000 && written < data.size(); i++ ) {
      written += p.write( 0, id, data.substr( written ) );
      p.carry();
      p.tick( 1 );
    }
    p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] );
    p.carry();
    p.connecting.read( id, NULL, 0, p.now, p.link, p.out[1] );
    p.carry();
    p.listening.segment( 0, p.last_from[1], p.now, p.link, p.out[0] );
    p.carry(); /* the reset arrives */
    unsigned int said = 0;
    for ( int i = 0; i < 12; i++ ) {
      p.out[1].takes = 100000;
      p.tick( 10 );
      said += p.out[1].sent.size();
      p.out[1].sent.clear();
    }
    check( "told the other end has gone, a window that opens is not announced to it: nothing is sent, and all arrives",
           said == 0 && p.out[1].got == data );
  }

  /* A reset while the other end has not finished is still believed: what it had to say is not all here. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.out[1].takes = 0;
    p.write( 0, id, pattern( 5000 ) );
    p.carry();
    TCPInUDP reset = p.last_from[0];
    reset.flags = TCPInUDP::RST;
    reset.seq = reset.seq + reset.payload.size();
    reset.payload.clear();
    p.connecting.segment( 0, reset, p.now, p.link, p.out[1] );
    check( "a reset from an end that has not finished: believed, and the connection ends with a reset",
           p.out[1].closed.size() == 1 && p.out[1].closed[0].second && p.out[1].got.empty() );
    (void) id;
  }
}

/* Segments that arrive in the opposite order, and twice: everything still arrives once, whole. */
static void test_out_of_order_and_twice( void )
{
  for ( int how = 0; how < 3; how++ ) {
    Pair p;
    p.backwards = how != 1;
    p.twice = how != 0;
    const uint32_t id = p.open();
    const std::string there = pattern( 300000 ), back = pattern( 70001 );
    size_t a = 0, b = 0;
    for ( int i = 0; i < 20000 && ( a < there.size() || b < back.size() ); i++ ) {
      a += p.write( 0, id, there.substr( a ) );
      b += p.write( 1, id, back.substr( b ) );
      p.carry();
      p.tick( 1 );
    }
    for ( int i = 0; i < 2000 && ( p.out[1].got.size() < there.size() || p.out[0].got.size() < back.size() ); i++ ) {
      p.carry();
      p.tick( 10 );
    }
    p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] );
    p.connecting.read( id, NULL, 0, p.now, p.link, p.out[1] );
    for ( int i = 0; i < 200 && ( p.listening.count() || p.connecting.count() ); i++ ) {
      p.carry();
      p.tick( 10 );
    }
    check( how == 0 ? "every batch of segments arriving backwards: both ways whole, once, and closed in order"
           : how == 1 ? "every segment arriving twice: likewise"
                      : "backwards and twice: likewise",
           p.out[1].got == there && p.out[0].got == back && p.out[0].closed.size() == 1 && !p.out[0].closed[0].second
             && p.out[1].closed.size() == 1 && !p.out[1].closed[0].second );
  }
}

/* Two connections at once are two connections. */
static void test_two( void )
{
  Pair p;
  const uint32_t first = p.open();
  const uint32_t second = p.open();
  check( "a second connection has a name of its own", first != second && p.listening.count() == 2 && p.connecting.count() == 2 );
  const std::vector<uint32_t> readable = p.listening.readable();
  check( "both are worth reading, in the order they were opened",
         readable.size() == 2 && readable[0] == first && readable[1] == second );

  const std::string one = pattern( 40000 ), two = std::string( 30000, 'z' );
  size_t a = 0, b = 0;
  for ( int i = 0; i < 500 && ( a < one.size() || b < two.size() ); i++ ) {
    a += p.write( 0, first, one.substr( a ) );
    b += p.write( 0, second, two.substr( b ) );
    p.carry();
    p.tick( 1 );
  }
  p.carry();
  check( "each connection's bytes arrive on that connection and not the other",
         p.out[1].got_on[first] == one && p.out[1].got_on[second] == two );

  p.listening.failed( first, p.link, p.out[0] );
  p.carry();
  check( "ending one leaves the other", p.listening.count() == 1 && p.connecting.count() == 1 && p.listening.wants( second ) > 0 );
}

/* What arrives for a connection that is not as the segment supposes. */
static void test_unexpected( void )
{
  /* The connecting end has its own limit: the sixty-fifth SYN is refused. */
  {
    Pair p;
    for ( uint16_t port = 1; port <= 64; port++ ) {
      TCPInUDP syn;
      syn.source_port = 30000 + port;
      syn.flags = TCPInUDP::SYN;
      syn.seq = 777;
      syn.window = 65535;
      p.connecting.segment( 0, syn, p.now, p.link, p.out[1] );
    }
    const bool held = p.connecting.count() == 64 && p.out[1].connects == 64 && p.out[1].sent.empty();
    TCPInUDP syn;
    syn.source_port = 31000;
    syn.flags = TCPInUDP::SYN;
    syn.seq = 777;
    p.connecting.segment( 0, syn, p.now, p.link, p.out[1] );
    check( "the connecting end takes sixty-four; the sixty-fifth is refused with a reset and no target is asked for",
           held && p.connecting.count() == 64 && p.out[1].connects == 64 && p.out[1].sent.size() == 1
             && p.out[1].sent[0].second.flags == ( TCPInUDP::RST | TCPInUDP::ACK ) && p.out[1].sent[0].second.ack == 778 );
  }
  /* An acknowledgment of something never sent, while waiting for the other end to accept. */
  {
    Pair p;
    uint32_t id = 0;
    p.listening.opened( 0, p.now, p.link, p.out[0], id );
    const TCPInUDP ours = p.out[0].sent[0].second; /* our SYN */
    p.out[0].sent.clear();
    TCPInUDP wrong;
    wrong.dest_port = ours.source_port;
    wrong.flags = TCPInUDP::SYN | TCPInUDP::ACK;
    wrong.seq = 5;
    wrong.ack = ours.seq + 99; /* not our SYN */
    p.listening.segment( 0, wrong, p.now, p.link, p.out[0] );
    check( "an acceptance of a SYN we did not send is answered with a reset at the number it named, and changes nothing",
           p.out[0].sent.size() == 1 && p.out[0].sent[0].second.flags == TCPInUDP::RST
             && p.out[0].sent[0].second.seq == ours.seq + 99 && p.listening.count() == 1 && p.out[0].closed.empty() );
  }
}

static void test_target( void )
{
  {
    Pair p;
    p.out[1].reached = -1; /* the target refuses */
    uint32_t id = 0;
    p.listening.opened( 0, p.now, p.link, p.out[0], id );
    p.carry();
    p.tick( 1 );
    p.carry();
    check( "a target that refuses: the application is refused too, with a reset",
           p.out[0].closed.size() == 1 && p.out[0].closed[0].second && p.listening.count() == 0 && p.connecting.count() == 0 );
  }
  {
    Pair p;
    p.out[1].reached = 0; /* the target never answers */
    uint32_t id = 0;
    p.listening.opened( 0, p.now, p.link, p.out[0], id );
    p.carry();
    p.tick( 9998 ); /* (the number itself, not the code's name for it: ten seconds is what is required) */
    p.carry();
    check( "a target that has not answered yet: both ends wait", p.out[0].closed.empty() && p.connecting.count() == 1 );
    p.tick( 2 );
    p.carry();
    check( "after ten seconds without an answer, the application is refused",
           p.out[0].closed.size() == 1 && p.out[0].closed[0].second && p.connecting.count() == 0 );
  }
  {
    Pair p;
    p.out[1].has_target = false; /* nothing to reach */
    uint32_t id = 0;
    p.listening.opened( 0, p.now, p.link, p.out[0], id );
    p.carry();
    check( "a forward with no target: refused at once, and no connection is kept",
           p.out[0].closed.size() == 1 && p.out[0].closed[0].second && p.connecting.count() == 0 );
  }
}

static void test_limits( void )
{
  Pair p;
  uint32_t id = 0;
  bool all = true;
  for ( size_t i = 0; i < 64; i++ ) { /* (the number itself: requirements, Operating envelope) */
    all = all && p.listening.opened( 0, p.now, p.link, p.out[0], id );
  }
  check( "sixty-four connections open", all && p.listening.count() == 64 );
  check( "the sixty-fifth is refused, and nothing is sent for it",
         ( p.out[0].sent.clear(), !p.listening.opened( 0, p.now, p.link, p.out[0], id ) ) && p.out[0].sent.empty() );

  Pair q;
  q.link.ready = false;
  check( "while the session cannot carry a segment, a connection is refused",
         !q.listening.opened( 0, q.now, q.link, q.out[0], id ) && q.listening.count() == 0 );
}

static void test_outage( void )
{
  /* REQ1/FWD-3: out of contact for six hours, with data on its way. */
  {
    Pair p;
    const uint32_t id = p.open();
    const std::string data = pattern( 20000 );
    p.carrying = false;
    p.link.in_contact = false;
    p.write( 0, id, data );
    for ( int i = 0; i < 6 * 60 * 2; i++ ) {
      p.tick( 30000 );
      p.out[0].sent.clear(); /* (lost) */
    }
    check( "REQ1/FWD-3: six hours out of contact, and the connection is still there",
           p.out[0].closed.empty() && p.listening.count() == 1 && p.out[1].got.empty() );

    p.carrying = true;
    p.link.in_contact = true;
    p.tick( 1 ); /* contact returns */
    check( "REQ1/FWD-3: when contact returns it sends again at once, without waiting on a backed-off timer",
           !p.out[0].sent.empty() );
    for ( int i = 0; i < 200 && p.out[1].got.size() < data.size(); i++ ) {
      p.carry();
      p.tick( 50 );
    }
    check( "REQ1/FWD-3: and what was written before the outage arrives whole", p.out[1].got == data );
  }
  /* The same silence while in contact is a different thing: the other end has gone. */
  {
    Pair p;
    const uint32_t id = p.open();
    p.carrying = false;
    p.write( 0, id, pattern( 20000 ) );
    for ( int i = 0; i < 6 * 60 * 2 && p.out[0].closed.empty(); i++ ) {
      p.tick( 30000 );
      p.out[0].sent.clear();
    }
    check( "in contact, with nothing ever acknowledged: the connection is ended, with a reset",
           p.out[0].closed.size() == 1 && p.out[0].closed[0].second && p.listening.count() == 0 );
  }
}

static void test_application_gone( void )
{
  Pair p;
  const uint32_t id = p.open();
  p.out[1].gone = true;
  p.write( 0, id, "anyone there" );
  p.carry();
  check( "an application that has gone: both ends close with a reset",
         p.out[1].closed.size() == 1 && p.out[1].closed[0].second && p.out[0].closed.size() == 1 && p.out[0].closed[0].second );

  Pair q;
  const uint32_t id2 = q.open();
  q.listening.failed( id2, q.link, q.out[0] );
  q.carry();
  check( "a socket that fails on reading: the same, from the other side",
         q.out[0].closed.size() == 1 && q.out[0].closed[0].second && q.out[1].closed.size() == 1 && q.out[1].closed[0].second );
}

static const size_t SEGMENT = 1187 - TCPInUDP::DATAGRAM_HEADER_LEN; /* the data one segment carries here */

/* Opens a connection and sends enough over it, with nothing lost, that the sender will send 40 segments at once. */
static uint32_t warmed( Pair& p, std::string& sent )
{
  const uint32_t id = p.open();
  sent = pattern( 300000 );
  size_t a = 0;
  for ( int i = 0; i < 2000 && a < sent.size(); i++ ) {
    a += p.write( 0, id, sent.substr( a ) );
    p.carry();
    p.tick( 1 );
  }
  p.carry();
  return id;
}

/*
 * Forty segments are sent at once and these of them (and of what is sent again after: 40 is
 * the first segment sent again) are lost. Returns how many bytes of data the sender sent in all
 * to get the forty there; and how long it took, and in how many exchanges.
 */
static size_t forty( bool sack, const std::vector<unsigned int>& gone, uint64_t& took, unsigned int& said_held,
                     unsigned int& exchanges, bool& whole, bool& burst )
{
  Pair p( sack, sack );
  std::string sent;
  const uint32_t id = warmed( p, sent );
  const std::string more = pattern( 40 * SEGMENT );
  sent += more;

  p.reset_count();
  p.lose = gone;
  const uint64_t began = p.now;
  const size_t written = p.write( 0, id, more );
  burst = written == more.size() && p.out[0].sent.size() == 40;
  for ( int i = 0; i < 100000 && p.out[1].got.size() < sent.size(); i++ ) {
    p.carry();
    p.tick( 1 );
  }
  took = p.now - began;
  said_held = p.said_held;
  exchanges = p.exchanges;
  whole = p.out[1].got == sent;
  return p.data_bytes;
}

static void test_selective_acknowledgment( void )
{
  uint64_t took = 0;
  unsigned int said_held = 0, exchanges = 0;
  bool whole = false, burst = false;
  const unsigned int apart[5] = { 3, 9, 15, 21, 27 };
  const std::vector<unsigned int> five( apart, apart + 5 );

  const size_t with = forty( true, five, took, said_held, exchanges, whole, burst );
  check( "(forty segments are sent at once)", burst );
  check( "five of forty segments lost, apart: everything arrives whole", whole );
  check( "the receiver said what it held beyond each gap", said_held >= 35 );
  check( "the sender sent again the five that were lost and nothing else: 45 segments of data",
         with == 45 * SEGMENT );
  check( "and without waiting for a timer: in under 200 ms of the logic's time", took < 200 );
  /* the forty; the five again, each as an acknowledgment says another segment got there; (and the last ACK answered) */
  check( "in two exchanges: all five were sent again before any of them was acknowledged", exchanges == 2 );

  const size_t without = forty( false, five, took, said_held, exchanges, whole, burst );
  check( "without selective acknowledgment (the fork's way) it arrives whole too", whole && burst );
  check( "with nothing said of what is held", said_held == 0 );
  check( "and more is sent again than was lost", without > 45 * SEGMENT );
  check( "in more exchanges", exchanges > 2 );
  printf( "    (without it: %lu segments' worth of data sent for 40, in %u exchanges)\n",
          static_cast<unsigned long>( ( without + SEGMENT - 1 ) / SEGMENT ), exchanges );

  /*
   * Two lost, and lost again when sent again: only a timer gets the sender going, and it starts
   * from the beginning. What the receiver holds is still known, and is passed over.
   */
  const unsigned int twice[4] = { 3, 9, 40, 41 };
  const std::vector<unsigned int> again( twice, twice + 4 );
  const size_t slow = forty( true, again, took, said_held, exchanges, whole, burst );
  check( "two lost, and lost again when sent again: it arrives whole, after a timer", whole && burst && took >= 200 );
  check( "and going back, the sender passed over what was held: 44 segments of data in all", slow == 44 * SEGMENT );
  const size_t slow_without = forty( false, again, took, said_held, exchanges, whole, burst );
  check( "(without selective acknowledgment: whole, and more than 44)", whole && slow_without > 44 * SEGMENT );

  /*
   * Three, likewise. After the timer the sender may send one segment, then two: the second gap,
   * and the third with it, because the five segments held between them are not on their way and
   * do not count against what it may have outstanding.
   */
  const unsigned int thrice[6] = { 3, 9, 15, 40, 41, 42 };
  const std::vector<unsigned int> three( thrice, thrice + 6 );
  const size_t slower = forty( true, three, took, said_held, exchanges, whole, burst );
  check( "three lost, and lost again: whole, with 46 segments of data in all", whole && slower == 46 * SEGMENT );
  check( "the second and third gaps are filled in one exchange after the first: 4 in all", exchanges == 4 );
}

/* Two megabytes with five segments in a hundred lost on the way. Returns the bytes of data sent in all. */
static size_t lossy( bool sack0, bool sack1, bool& whole, Pair** kept = NULL )
{
  Pair* p = new Pair( sack0, sack1 );
  const uint32_t id = p->open();
  const std::string data = pattern( 2 * 1024 * 1024 );
  p->reset_count();
  p->lose_in_1000 = 50;
  size_t a = 0;
  for ( int i = 0; i < 2000000 && p->out[1].got.size() < data.size(); i++ ) {
    a += p->write( 0, id, data.substr( a, 65536 ) );
    p->carry();
    p->tick( 1 );
  }
  whole = p->out[1].got == data;
  const size_t sent = p->data_bytes;
  if ( kept ) {
    *kept = p;
  } else {
    delete p;
  }
  return sent;
}

static void test_lossy( void )
{
  bool whole = false;
  const size_t data = 2 * 1024 * 1024;

  const size_t with = lossy( true, true, whole );
  check( "2 MB with 5 in 100 segments lost: it arrives whole", whole );
  /* 5 in 100 lost means 100 sent for 95 to arrive, 105.3% at the least; what is over that was sent for nothing. */
  check( "having sent less than 108% of it", with < data / 100 * 108 );
  printf( "    (with selective acknowledgment: %.1f%% of the data was sent)\n", 100.0 * with / data );

  const size_t without = lossy( false, false, whole );
  check( "without selective acknowledgment it arrives whole", whole );
  check( "having sent more than that", without > with );
  printf( "    (without: %.1f%%)\n", 100.0 * without / data );

  /* An end that does not know selective acknowledgment: pf.7's, at either end. */
  for ( int knows = 0; knows < 2; knows++ ) {
    Pair* p = NULL;
    const size_t mixed = lossy( knows == 0, knows == 1, whole, &p );
    check( knows == 0 ? "the target's end does not know it: 2 MB with losses arrives whole"
                      : "the listening end does not know it: 2 MB with losses arrives whole",
           whole );
    check( "  and no segment after the handshake carries any option, either way",
           p->options_after_syn[0] == 0 && p->options_after_syn[1] == 0 );
    check( "  and as much is sent as when neither end knows it: it is the fork's TCP", mixed == without );
    delete p;
  }
}

/*
 * An end that says it holds a segment it never got. The sender has no reason to send that
 * segment again until the acknowledgments reach it and stop there; an end that held it would
 * have acknowledged past it, so from then the claim is not believed, and it is sent.
 */
static void test_lying_holder( void )
{
  Pair p;
  std::string sent;
  const uint32_t id = warmed( p, sent );
  const std::string more = pattern( 40 * SEGMENT ), last = pattern( SEGMENT );
  sent += more + last;
  p.reset_count();
  /*
   * The lie is about the last of the forty, the only one lost. Nothing arrives after it, so
   * nothing prompts the sender but its timer; and the lie is told both while the segment is
   * ahead of what is acknowledged and, with one more segment sent after, when it is next.
   */
  p.lose.push_back( 39 );
  p.lying = true;
  p.write( 0, id, more );
  p.carry();
  p.write( 0, id, last );
  const uint64_t began = p.now;
  for ( int i = 0; i < 20000 && p.out[1].got.size() < sent.size() && p.out[0].closed.empty(); i++ ) {
    p.carry();
    p.tick( 10 );
  }
  check( "(the lie was told)", p.lie_known && p.said_held > 0 );
  check( "an end that says it holds what it never got: everything arrives whole all the same", p.out[1].got == sent );
  check( "  within a minute of the logic's time, and the connection stays",
         p.now - began < 60 * 1000 && p.out[0].closed.empty() && p.out[1].closed.empty() );
}

/* What the receiver says it holds: four ranges at most, the latest arrival's first, then the nearest. */
static void test_what_is_said( void )
{
  Pair p;
  std::string sent;
  const uint32_t id = warmed( p, sent );
  const std::string more = pattern( 40 * SEGMENT );
  p.reset_count();
  const unsigned int apart[5] = { 3, 9, 15, 21, 27 };
  p.lose.assign( apart, apart + 5 );
  p.lose_acknowledgments = 1000; /* (none gets back, so nothing is sent again: this is the forty arriving, alone) */
  p.write( 0, id, more );
  /* carry the forty over, one way only */
  std::vector<std::pair<uint8_t, TCPInUDP> > forty;
  forty.swap( p.out[0].sent );
  uint32_t first = 0;
  for ( size_t i = 0; i < forty.size(); i++ ) {
    if ( i == 0 ) {
      first = forty[i].second.seq;
    }
    if ( !p.lost( 0, forty[i].second ) ) {
      p.connecting.segment( forty[i].first, forty[i].second, p.now, p.link, p.out[1] );
    }
  }
  const std::string said = p.out[1].sent.empty() ? "" : p.out[1].sent.back().second.options;
  /* held: segments 4-8, 10-14, 16-20, 22-26, 28-39. The last to arrive was 39. */
  std::string expected;
  const char head[4] = { 1, 1, 5, 34 };
  expected.assign( head, 4 );
  const unsigned int ranges[4][2] = { { 28, 40 }, { 4, 9 }, { 10, 15 }, { 16, 21 } };
  for ( int r = 0; r < 4; r++ ) {
    for ( int k = 0; k < 2; k++ ) {
      const uint32_t v = first + ranges[r][k] * SEGMENT;
      const char b[4] = { static_cast<char>( v >> 24 ), static_cast<char>( v >> 16 ), static_cast<char>( v >> 8 ),
                          static_cast<char>( v ) };
      expected.append( b, 4 );
    }
  }
  check( "five gaps: the last acknowledgment names the latest range, then the three nearest, exactly", said == expected );

  /*
   * Only that last acknowledgment gets back. One is not enough to start a repair, so a timer
   * does; and going back, the sender passes over the four ranges it was told of, and is told of
   * the fifth by the acknowledgment of the first gap before it gets there.
   */
  p.out[1].sent.erase( p.out[1].sent.begin(), p.out[1].sent.end() - 1 );
  p.lose_acknowledgments = 0;
  p.lose.clear();
  for ( int i = 0; i < 100000 && p.out[1].got.size() < sent.size() + more.size(); i++ ) {
    p.carry();
    p.tick( 1 );
  }
  check( "with only that acknowledgment back: whole, and the five lost sent again and nothing else, 45 in all",
         p.out[1].got == sent + more && p.data_bytes == 45 * SEGMENT );
}

/*
 * What is held is not on its way, and does not count against how much the sender may have on
 * its way. But it is in the receiver's buffer, or will be, and counts against the room the
 * receiver has said it has: the sender never sends past that, a loss or no.
 */
static void test_loss_and_a_full_receiver( void )
{
  Pair p;
  const uint32_t id = p.open();
  p.out[1].takes = 0; /* the target takes nothing */
  const std::string data = pattern( 1300000 );
  size_t written = 0;
  for ( int i = 0; i < 400 && written < 900000; i++ ) {
    written += p.write( 0, id, data.substr( written, 900000 - written ) );
    p.carry();
    p.tick( 1 );
  }
  check( "(900,000 bytes wait in the receiver's buffer)", written == 900000 && p.out[1].got.empty() );
  p.reset_count();
  p.lose.push_back( 0 ); /* the next segment is lost */
  for ( int i = 0; i < 100 && written < data.size(); i++ ) {
    written += p.write( 0, id, data.substr( written ) );
    p.carry();
    p.tick( 1 );
  }
  check( "a segment lost while the receiver is nearly full: nothing is sent past the room it gave (but a 1-byte probe)",
         p.beyond_edge <= 1 );
  p.out[1].takes = -1;
  for ( int i = 0; i < 100000 && p.out[1].got.size() < written; i++ ) {
    written += p.write( 0, id, data.substr( written ) );
    p.carry();
    p.tick( 1 );
  }
  check( "and when the target takes again, everything arrives whole", written == data.size() && p.out[1].got == data );
}

/* An end that says it holds our FIN, which is no data of ours: not believed, and nothing breaks. */
static void test_held_past_the_end( void )
{
  Pair p;
  std::string sent;
  const uint32_t id = warmed( p, sent );
  p.reset_count();
  p.lose.push_back( 0 );
  p.lying = true;
  p.lie_from_shift = 5; /* of ten bytes lost, it says it holds from the sixth... */
  p.lie_to_shift = 1;   /* ...to one past the last: the FIN's place */
  const std::string last = pattern( 10 );
  p.write( 0, id, last );
  p.listening.read( id, NULL, 0, p.now, p.link, p.out[0] ); /* and the application finishes */
  for ( int i = 0; i < 20000 && !p.out[1].shut; i++ ) {
    p.carry();
    p.tick( 10 );
  }
  check( "(the lie was told)", p.lie_known );
  check( "an end that says it holds past the end of our data: the ten bytes and the finish arrive all the same",
         p.out[1].got == sent + last && p.out[1].shut && p.out[0].closed.empty() );
}

int main()
{
  test_transfer();
  test_slow_application();
  test_window();
  test_handshake_gives_up();
  test_slow_finish();
  test_reset_after_everything_arrived();
  test_out_of_order_and_twice();
  test_two();
  test_unexpected();
  test_target();
  test_limits();
  test_outage();
  test_application_gone();
  test_selective_acknowledgment();
  test_lossy();
  test_lying_holder();
  test_what_is_said();
  test_loss_and_a_full_receiver();
  test_held_past_the_end();

  if ( failures ) {
    printf( "FAIL REQ1/FWD-1, REQ1/FWD-3: %d checks of the connection logic failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/FWD-1, REQ1/FWD-3: the connection logic, with no socket and no clock\n" );
  return EXIT_SUCCESS;
}
