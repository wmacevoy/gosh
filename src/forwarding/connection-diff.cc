/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/*
 * REQ1/FWD-1, REQ1/FWD-3, REQ6/SHAPE-5: the reshaped connection logic (connection.h) does what
 * the fork's adapter (AdapterPort, tcpadapter.cc) does, segment for segment.
 *
 * usage: connection-diff [SCENARIOS [STEPS [FIRST_SEED]]]      (500, 600 and 1 by default)
 *
 * Each scenario is a script made from a seed: an application at each end writing and closing,
 * segments delivered, dropped, repeated or held back, time passing in steps from nothing to
 * hours, contact with the other end lost and found, the largest datagram changing, and stray
 * segments for connections that do not exist. The same script is given to two worlds:
 *
 *   old   two of the fork's AdapterPort, one listening and one connecting, each on a real socket
 *         pair, with the times it reads from the clock supplied by this test;
 *   new   two Connections, with no socket at all.
 *
 * After every step both worlds must have emitted exactly the same segments, delivered exactly
 * the same bytes to their applications, hold the same number of connections, and want waking at
 * the same time. The first difference stops the test and prints the seed and the step.
 *
 * What it does not reach: an application too slow to take what is delivered (a real socket pair
 * always has room here), a target that refuses or does not answer, more than one connection at
 * once, and running out of connections. connection-test.cc has those, of the new logic alone.
 *
 * The test reads the old class's private members to learn each connection's first sequence
 * number, which the old class draws at random and the new logic is given: with the same number
 * in both worlds, "the same segments" means the same bytes.
 */

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/crypto/prng.h"
#include "src/forwarding/connection.h"
#include "src/forwarding/tcpforward.h"
#include "src/forwarding/tcpinudp.h"
#include "src/forwarding/udpforward.h"
#include "src/network/network.h"
#include "src/util/timestamp.h"

/* (to read each old connection's first sequence number; see above) */
#define private public
#include "src/forwarding/tcpadapter.h"
#undef private

using Network::TCPInUDP;

/* The clock the old class reads (Network::timestamp() is this), in place of the core's own. */
static uint64_t virtual_now = 1000000;
uint64_t frozen_timestamp( void )
{
  return virtual_now;
}
void freeze_timestamp( void ) {}

typedef std::pair<uint8_t, TCPInUDP> Sent; /* a forward, and a segment for the other end */

static std::string show( const Sent& s )
{
  char buf[200];
  snprintf( buf, sizeof( buf ), "forward %u ports %u>%u flags %03x seq %u ack %u window %u options %zu payload %zu",
            s.first, s.second.source_port, s.second.dest_port, s.second.flags, s.second.seq, s.second.ack,
            s.second.window, s.second.options.size(), s.second.payload.size() );
  return buf;
}

static bool same( const Sent& a, const Sent& b )
{
  return a.first == b.first && a.second.source_port == b.second.source_port && a.second.dest_port == b.second.dest_port
         && a.second.flags == b.second.flags && a.second.seq == b.second.seq && a.second.ack == b.second.ack
         && a.second.window == b.second.window && a.second.options == b.second.options
         && a.second.payload == b.second.payload;
}

/* ---- the old world: one end */

class OldSink : public Network::SegmentSink
{
public:
  bool ready, up;
  size_t largest;
  std::vector<Sent> sent;

  OldSink() : ready( true ), up( true ), largest( 1187 ), sent() {}
  bool segment_ready( void ) const { return ready; }
  bool link_up( void ) const { return up; }
  size_t max_datagram( void ) const { return largest; }
  void to_network( uint8_t forward, const TCPInUDP& segment ) { sent.push_back( Sent( forward, segment ) ); }
};

struct OldEnd
{
  OldSink sink;
  Network::AdapterPort port;
  int app, adapter; /* the two ends of the socket pair: the application's, and the adapter's */

  explicit OldEnd( bool listening ) : sink(), port( &sink, listening ), app( -1 ), adapter( -1 ) {}
  ~OldEnd()
  {
    if ( app >= 0 ) {
      close( app );
    }
  }

  /* Makes the pair (a new application, if there was one before); returns the adapter's end, which the adapter will own. */
  int make_pair( void )
  {
    if ( app >= 0 ) {
      close( app );
    }
    int fds[2];
    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, fds ) < 0 ) {
      perror( "socketpair" );
      exit( 2 );
    }
    app = fds[0];
    adapter = fds[1];
    fcntl( app, F_SETFL, fcntl( app, F_GETFL ) | O_NONBLOCK );
    return adapter;
  }

  /* Everything the adapter has delivered to the application since last asked. */
  std::string taken( void )
  {
    std::string all;
    char buf[65536];
    while ( app >= 0 ) {
      const ssize_t n = recv( app, buf, sizeof( buf ), MSG_DONTWAIT );
      if ( n <= 0 ) {
        break;
      }
      all.append( buf, n );
    }
    return all;
  }
};

/* ---- the new world: one end */

struct NewOut
{
  std::vector<Sent> sent;
  std::deque<uint32_t> sequences; /* learnt from the old world, in the order they will be asked for */
  std::string delivered;
  unsigned int targets_asked; /* how many times a target has been asked for: each is a new application */

  NewOut() : sent(), sequences(), delivered(), targets_asked( 0 ) {}

  void send( uint8_t forward, const TCPInUDP& segment ) { sent.push_back( Sent( forward, segment ) ); }
  uint32_t fresh_sequence( void )
  {
    if ( sequences.empty() ) {
      fprintf( stderr, "the new logic asked for a sequence number where the old drew none\n" );
      exit( 1 );
    }
    const uint32_t s = sequences.front();
    sequences.pop_front();
    return s;
  }
  bool connect( uint32_t, uint8_t )
  {
    targets_asked++;
    return true;
  }
  int connecting( uint32_t ) { return 1; } /* a socket pair is connected from the start */
  long deliver( uint32_t, const char* data, size_t length )
  {
    delivered.append( data, length );
    return length;
  }
  void shut_write( uint32_t ) {}
  void close( uint32_t, bool ) {}
};

struct NewEnd
{
  NewOut out;
  Gosh::Connections<NewOut> connections;
  std::string pending; /* written by the application, not yet read from its socket */
  bool finished;       /* the application has finished writing */

  explicit NewEnd( bool listening ) : out(), connections( listening ), pending(), finished( false ) {}
};

/* ---- a scenario */

struct Random
{
  uint32_t state;
  explicit Random( uint32_t seed ) : state( seed * 2654435761u + 1 ) {}
  uint32_t next( void )
  {
    state = state * 1664525 + 1013904223;
    return state >> 8;
  }
  uint32_t below( uint32_t n ) { return next() % n; }
};

static void fail( unsigned int seed, unsigned int step, const char* what, const std::string& old_one,
                  const std::string& new_one )
{
  printf( "FAIL REQ1/FWD-1: seed %u, step %u: %s\n  old: %s\n  new: %s\n", seed, step, what, old_one.c_str(),
          new_one.c_str() );
  exit( EXIT_FAILURE );
}

static std::string number( uint64_t n )
{
  return std::to_string( n );
}

/* Returns how many segments the scenario exchanged. */
static size_t scenario( unsigned int seed, unsigned int steps )
{
  Random random( seed );
  virtual_now = 1000000;

  OldEnd old_end[2] = { OldEnd( true ), OldEnd( false ) }; /* 0 listens, 1 connects to the target */
  NewEnd new_end[2] = { NewEnd( true ), NewEnd( false ) };
  Gosh::Link link[2] = { { true, true, 1187 }, { true, true, 1187 } };
  std::deque<Sent> old_wire[2], new_wire[2]; /* [0]: from end 0 to end 1; [1]: the other way */
  std::string old_app[2];                    /* what each old application has been given, so far */
  const uint8_t forward = 0;
  uint32_t id = 0; /* the new world's name for the connection */
  size_t exchanged = 0;
  unsigned int targets_seen = 0;

  /* Server: the target is the other end of a socket pair. */
  old_end[1].port.add_spawner( forward, [&]() { return old_end[1].make_pair(); } );

  /* The application connects, at the listening end. */
  {
    const size_t before = old_end[0].port.conns.size();
    const bool old_ok = old_end[0].port.open_with( forward, old_end[0].make_pair(), virtual_now );
    if ( old_end[0].port.conns.size() > before ) {
      new_end[0].out.sequences.push_back( old_end[0].port.conns.back().iss );
    }
    const bool new_ok = new_end[0].connections.opened( forward, virtual_now, link[0], new_end[0].out, id );
    if ( old_ok != new_ok ) {
      fail( seed, 0, "opening a connection", number( old_ok ), number( new_ok ) );
    }
  }

  for ( unsigned int step = 1; step <= steps; step++ ) {
    const unsigned int choice = random.below( 100 );
    const unsigned int end = random.below( 2 );

    if ( choice < 22 ) {
      /* an application writes */
      const size_t length = random.below( 10 ) == 0 ? 20000 + random.below( 30000 ) : 1 + random.below( 3000 );
      if ( old_end[end].app >= 0 && !new_end[end].finished && new_end[end].pending.size() < 60000 ) {
        std::string data( length, 0 );
        for ( size_t i = 0; i < length; i++ ) {
          data[i] = static_cast<char>( random.next() );
        }
        if ( send( old_end[end].app, data.data(), data.size(), MSG_NOSIGNAL | MSG_DONTWAIT )
             == static_cast<ssize_t>( data.size() ) ) {
          new_end[end].pending += data;
        }
        /* (if the old adapter has closed its end, the write fails in the old world and is not made in the new) */
      }
    } else if ( choice < 25 ) {
      /* an application finishes writing */
      if ( old_end[end].app >= 0 && !new_end[end].finished ) {
        shutdown( old_end[end].app, SHUT_WR );
        new_end[end].finished = true;
      }
    } else if ( choice < 70 ) {
      /* the next segment on its way to `end` arrives, or does not */
      const unsigned int from = 1 - end;
      if ( !old_wire[from].empty() ) {
        Sent old_one = old_wire[from].front();
        Sent new_one = new_wire[from].front();
        old_wire[from].pop_front();
        new_wire[from].pop_front();
        const unsigned int fate = random.below( 100 );
        if ( fate < 8 ) {
          /* lost */
        } else if ( fate < 14 ) {
          old_wire[from].push_back( old_one ); /* held back: it arrives after what follows it */
          new_wire[from].push_back( new_one );
        } else {
          const unsigned int times = fate < 20 ? 2 : 1; /* sometimes twice */
          for ( unsigned int t = 0; t < times; t++ ) {
            const size_t before = old_end[end].port.conns.size();
            old_end[end].port.from_network( old_one.first, old_one.second );
            if ( old_end[end].port.conns.size() > before ) {
              new_end[end].out.sequences.push_back( old_end[end].port.conns.back().iss );
            }
            new_end[end].connections.segment( new_one.first, new_one.second, virtual_now, link[end], new_end[end].out );
            exchanged++;
          }
        }
      }
    } else if ( choice < 90 ) {
      /* time passes */
      static const uint64_t STEPS[] = { 0, 1, 3, 10, 40, 200, 250, 1000, 3000, 31000, 6 * 3600 * 1000 };
      const unsigned int which = random.below( 100 );
      virtual_now += STEPS[which < 70 ? random.below( 7 ) : which < 97 ? 7 + random.below( 3 ) : 10];
      for ( unsigned int e = 0; e < 2; e++ ) {
        old_end[e].port.tick( virtual_now );
        new_end[e].connections.tick( virtual_now, link[e], new_end[e].out );
      }
    } else if ( choice < 94 ) {
      /* contact with the other end is lost, or found */
      old_end[end].sink.up = !old_end[end].sink.up;
      link[end].in_contact = old_end[end].sink.up;
    } else if ( choice < 97 ) {
      /* the largest datagram changes */
      static const size_t SIZES[] = { 1187, 600, 100, 70 };
      old_end[end].sink.largest = SIZES[random.below( 4 )];
      link[end].max_datagram = old_end[end].sink.largest;
    } else {
      TCPInUDP stray;
      if ( random.below( 4 ) == 0 && !old_end[end].port.conns.empty() ) {
        /* a reset for the connection there is, at or near where the other end should be:
           believed only within 65,535 of it */
        static const uint32_t OFF[] = { 0, 0x800, 0x2000, 0xFFFF, 0x10000, 0x1FFFF };
        ( end == 0 ? stray.dest_port : stray.source_port ) = old_end[end].port.conns.front().port;
        stray.flags = TCPInUDP::RST;
        stray.seq = old_end[end].port.conns.front().rcv_nxt + OFF[random.below( 6 )];
      } else {
        /* a segment for a connection that does not exist */
        ( end == 0 ? stray.dest_port : stray.source_port ) = 40000 + random.below( 4 );
        static const uint16_t FLAGS[] = { TCPInUDP::ACK, TCPInUDP::RST, TCPInUDP::FIN | TCPInUDP::ACK,
                                          TCPInUDP::SYN | TCPInUDP::ACK, TCPInUDP::RST | TCPInUDP::ACK, 0 };
        stray.flags = FLAGS[random.below( 6 )];
        stray.seq = random.next();
        stray.ack = random.next();
        stray.payload.assign( random.below( 5 ), 'x' );
      }
      old_end[end].port.from_network( forward, stray );
      new_end[end].connections.segment( forward, stray, virtual_now, link[end], new_end[end].out );
    }

    /*
     * A target asked for again (the first connection died, and the other end's SYN came again)
     * is a new application on a new socket in the old world: nothing written, nothing finished.
     */
    if ( new_end[1].out.targets_asked != targets_seen ) {
      targets_seen = new_end[1].out.targets_asked;
      new_end[1].pending.clear();
      new_end[1].finished = false;
    }

    /* Each adapter reads what its application has written, as a program's loop would have it do. */
    for ( unsigned int e = 0; e < 2; e++ ) {
      if ( old_end[e].adapter >= 0 ) {
        old_end[e].port.read_fd( old_end[e].adapter );
      }
      NewEnd& n = new_end[e];
      const size_t want = n.connections.wants( id );
      if ( want ) {
        if ( !n.pending.empty() ) {
          const size_t take = std::min( want, n.pending.size() );
          n.connections.read( id, n.pending.data(), take, virtual_now, link[e], n.out );
          n.pending.erase( 0, take );
        } else if ( n.finished ) {
          n.connections.read( id, NULL, 0, virtual_now, link[e], n.out );
        }
      }
    }

    /* The two worlds must now agree on everything that can be seen from outside. */
    for ( unsigned int e = 0; e < 2; e++ ) {
      const std::vector<Sent>& o = old_end[e].sink.sent;
      const std::vector<Sent>& n = new_end[e].out.sent;
      for ( size_t i = 0; i < std::max( o.size(), n.size() ); i++ ) {
        if ( i >= o.size() || i >= n.size() || !same( o[i], n[i] ) ) {
          fail( seed, step, e == 0 ? "a segment from the listening end" : "a segment from the connecting end",
                i < o.size() ? show( o[i] ) : "(none)", i < n.size() ? show( n[i] ) : "(none)" );
        }
        old_wire[e].push_back( o[i] );
        new_wire[e].push_back( n[i] );
      }
      old_end[e].sink.sent.clear();
      new_end[e].out.sent.clear();

      old_app[e] += old_end[e].taken();
      if ( old_app[e] != new_end[e].out.delivered ) {
        fail( seed, step, "the bytes delivered to an application", number( old_app[e].size() ) + " bytes",
              number( new_end[e].out.delivered.size() ) + " bytes" );
      }
      if ( old_end[e].port.connection_count() != new_end[e].connections.count() ) {
        fail( seed, step, "how many connections there are", number( old_end[e].port.connection_count() ),
              number( new_end[e].connections.count() ) );
      }
      if ( old_end[e].port.wait_time( virtual_now ) != new_end[e].connections.wait_time( virtual_now ) ) {
        fail( seed, step, "when it wants waking", number( old_end[e].port.wait_time( virtual_now ) ),
              number( new_end[e].connections.wait_time( virtual_now ) ) );
      }
      /* (the old adapter lists its listeners' and connections' sockets; here there are no listeners) */
      if ( old_end[e].port.fds().size() != new_end[e].connections.readable().size() ) {
        fail( seed, step, "whether its socket is worth reading", number( old_end[e].port.fds().size() ),
              number( new_end[e].connections.readable().size() ) );
      }
    }
  }
  return exchanged;
}

int main( int argc, char* argv[] )
{
  const unsigned int scenarios = argc > 1 ? atoi( argv[1] ) : 500;
  const unsigned int steps = argc > 2 ? atoi( argv[2] ) : 600;
  const unsigned int first = argc > 3 ? atoi( argv[3] ) : 1;

  size_t exchanged = 0;
  for ( unsigned int seed = first; seed < first + scenarios; seed++ ) {
    exchanged += scenario( seed, steps );
  }
  printf( "PASS REQ1/FWD-1, REQ1/FWD-3, REQ6/SHAPE-5: old and new agree over %u scenarios of %u steps, %zu segments delivered\n",
          scenarios, steps, exchanged );
  return EXIT_SUCCESS;
}
