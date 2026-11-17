/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* Tests UDP forwarding: specification parsing, the wire format,
   duplicate suppression, and a round trip through a pair of meetups
   joined by an in-memory side channel. */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/forwarding/forwarding.h"
#include "src/forwarding/udp/udpforward.h"
#include "src/util/timestamp.h"
#include "vendor/facts/facts.h"

/* The facts of the group now running, for checks outside a group: each group sets it, and a
   helper that checks takes it as its own facts. */
static Facts* current_facts = nullptr;

using namespace Network;

/* (gosh) The fork's UDPForwarder::encode and ::decode, which only its tests called: a frame of plain UDP data. */
static std::string encode_data( uint8_t forward, uint16_t flow_id, uint64_t seq, const std::string& datagram )
{
  return Gosh::encode_frame( Gosh::TYPE_UDP_DATA, forward, flow_id, seq, datagram );
}

static bool decode_data( const std::string& payload, uint8_t& forward, uint16_t& flow_id, uint64_t& seq, std::string& datagram )
{
  uint8_t type;
  return Gosh::decode_frame( payload, type, forward, flow_id, seq, datagram ) && type == Gosh::TYPE_UDP_DATA;
}


static bool spec_ok( const char* s )
{
  try {
    Gosh::ForwardSpec spec( s );
    return true;
  } catch ( const NetworkException& ) {
    return false;
  }
}
FACTS( Spec )
{
  current_facts = facts;
  Gosh::ForwardSpec a( "5353:ns.example.com:53" );
  FACT_CHECK( ( a.bind_host == "127.0.0.1" && a.bind_port == "5353" ) );
  FACT_CHECK( ( a.target_host == "ns.example.com" && a.target_port == "53" ) );

  Gosh::ForwardSpec b( "[::1]:5353:[2001:db8::1]:53" );
  FACT_CHECK( ( b.bind_host == "::1" && b.bind_port == "5353" ) );
  FACT_CHECK( ( b.target_host == "2001:db8::1" && b.target_port == "53" ) );

  Gosh::ForwardSpec c( "*:1:h:65535" );
  FACT_CHECK( ( c.bind_host == "*" && c.bind_port == "1" && c.target_port == "65535" ) );

  FACT_CHECK( !spec_ok( "" ) );
  FACT_CHECK( !spec_ok( "53" ) );
  FACT_CHECK( !spec_ok( "53:host" ) );
  FACT_CHECK( !spec_ok( "0:host:53" ) );
  FACT_CHECK( !spec_ok( "53:host:65536" ) );
  FACT_CHECK( !spec_ok( "53:host:dns" ) );
  FACT_CHECK( !spec_ok( "53::53" ) );
  FACT_CHECK( !spec_ok( "a:b:1:host:2" ) );
  FACT_CHECK( !spec_ok( "[::1:5353:host:53" ) );
}
FACTS( Wire )
{
  current_facts = facts;
  const std::string datagram( "hello\0world", 11 );
  std::string wire = encode_data( 7, 0xBEEF, 0x0102030405060708ULL, datagram );
  FACT_CHECK( wire.size() == Gosh::FRAME_HEADER_LEN + datagram.size() );
  FACT_CHECK( static_cast<uint8_t>( wire[0] ) == SIDECHANNEL_MAGIC );

  uint8_t forward;
  uint16_t flow;
  uint64_t seq;
  std::string out;
  FACT_CHECK( decode_data( wire, forward, flow, seq, out ) );
  FACT_CHECK( ( forward == 7 && flow == 0xBEEF && seq == 0x0102030405060708ULL && out == datagram ) );

  /* an empty datagram is a legitimate datagram */
  FACT_CHECK( decode_data( encode_data( 0, 0, 0, "" ), forward, flow, seq, out ) );
  FACT_CHECK( out.empty() );

  FACT_CHECK( !decode_data( wire.substr( 0, Gosh::FRAME_HEADER_LEN - 1 ), forward, flow, seq, out ) );
  wire[0] = 0;
  FACT_CHECK( !decode_data( wire, forward, flow, seq, out ) );
}
FACTS( Replay )
{
  current_facts = facts;
  const uint64_t W = Gosh::ReplayWindow::WINDOW;

  Gosh::ReplayWindow w;
  FACT_CHECK( w.accept( 5 ) );
  FACT_CHECK( !w.accept( 5 ) );
  FACT_CHECK( w.accept( 3 ) ); /* reordering is fine */
  FACT_CHECK( !w.accept( 3 ) );
  FACT_CHECK( w.accept( 6 ) );
  FACT_CHECK( w.accept( 4 ) );
  FACT_CHECK( ( !w.accept( 4 ) && !w.accept( 5 ) && !w.accept( 6 ) ) );

  /* everything is accepted exactly once across many windows */
  Gosh::ReplayWindow x;
  for ( uint64_t i = 0; i < 5 * W; i++ ) {
    FACT_CHECK( x.accept( i ) );
    FACT_CHECK( !x.accept( i ) );
    if ( i >= 10 ) {
      FACT_CHECK( !x.accept( i - 10 ) );
    }
  }

  /* skipped numbers stay acceptable only while inside the window */
  Gosh::ReplayWindow y;
  FACT_CHECK( y.accept( 0 ) );
  FACT_CHECK( y.accept( 2 ) );
  FACT_CHECK( y.accept( W ) );
  FACT_CHECK( y.accept( 1 ) ); /* W - 1 behind: still inside */
  FACT_CHECK( !y.accept( 0 ) );
  FACT_CHECK( y.accept( W + 2 ) );
  FACT_CHECK( ( !y.accept( 1 ) && !y.accept( 2 ) ) );
  FACT_CHECK( y.accept( 3 ) ); /* W - 1 behind */
  FACT_CHECK( y.accept( W + 3 ) );
  FACT_CHECK( !y.accept( 3 ) ); /* W behind: too old, and already seen anyway */

  /* a large jump forgets everything older */
  Gosh::ReplayWindow z;
  FACT_CHECK( z.accept( 1 ) );
  FACT_CHECK( z.accept( 100 * W ) );
  FACT_CHECK( !z.accept( 1 ) );
  FACT_CHECK( !z.accept( 100 * W ) );
  FACT_CHECK( z.accept( 100 * W - 1 ) );
}

static std::string varied( size_t n )
{
  std::string s( n, '\0' );
  for ( size_t i = 0; i < n; i++ ) {
    s[i] = static_cast<char>( ( i * 2654435761u ) >> 11 );
  }
  return s;
}

/* Puts fragments back together, leaving out those listed.  True if that rebuilt the datagram. */
static bool reassembles( const std::vector<std::string>& fragments,
                         const std::string& expect,
                         int skip_a = -1,
                         int skip_b = -1,
                         bool backwards = false,
                         bool* used_parity = NULL )
{
  Facts* facts = current_facts;
  Gosh::DatagramAssembly assembly;
  std::string out;
  bool parity = false;
  bool complete = false;
  for ( size_t n = 0; n < fragments.size(); n++ ) {
    const int i = backwards ? fragments.size() - 1 - n : n;
    if ( i == skip_a || i == skip_b ) {
      continue;
    }
    const Gosh::DatagramAssembly::Result r = assembly.add( fragments[i], out, parity );
    FACT_CHECK( r != Gosh::DatagramAssembly::REJECTED );
    if ( r == Gosh::DatagramAssembly::COMPLETE ) {
      FACT_CHECK( out == expect );
      complete = true;
      break;
    }
  }
  if ( used_parity ) {
    *used_parity = parity;
  }
  return complete;
}
FACTS( Fragments )
{
  current_facts = facts;
  const size_t H = Gosh::DatagramAssembly::FRAGMENT_HEADER_LEN;

  /* pieces are as equal as possible, and never over the limit */
  const std::string d = varied( 2500 );
  std::vector<std::string> f = Gosh::DatagramAssembly::split( d, 1000 + H, false );
  FACT_CHECK( f.size() == 3 );
  FACT_CHECK( ( f[0].size() == 834 + H && f[1].size() == 833 + H && f[2].size() == 833 + H ) );
  FACT_CHECK( ( reassembles( f, d ) && reassembles( f, d, -1, -1, true ) ) );
  FACT_CHECK( ( !reassembles( f, d, 0 ) && !reassembles( f, d, 1 ) && !reassembles( f, d, 2 ) ) );

  /* one parity fragment, as long as the longest piece; any one fragment can go missing */
  f = Gosh::DatagramAssembly::split( d, 1000 + H, true );
  FACT_CHECK( ( f.size() == 4 && f[3].size() == 834 + H ) );
  for ( int lost = 0; lost < 4; lost++ ) {
    bool parity;
    FACT_CHECK( ( reassembles( f, d, lost, -1, false, &parity ) && parity == ( lost < 3 ) ) );
    FACT_CHECK( reassembles( f, d, lost, -1, true ) );
  }
  FACT_CHECK( ( !reassembles( f, d, 0, 1 ) && !reassembles( f, d, 1, 3 ) && !reassembles( f, d, 0, 2 ) ) );

  /* every length around a boundary, with every single loss */
  for ( size_t len = 195; len <= 405; len++ ) {
    const std::string x = varied( len );
    f = Gosh::DatagramAssembly::split( x, 100 + H, true );
    FACT_CHECK( ( f.size() == ( len + 99 ) / 100 + 1 || len <= 100 ) );
    for ( size_t i = 0; i < f.size(); i++ ) {
      FACT_CHECK( f[i].size() <= 100 + H );
      FACT_CHECK( reassembles( f, x, i ) );
    }
  }

  /* more than eight pieces: a parity fragment for each group of eight */
  const std::string big = varied( 20 * 500 );
  f = Gosh::DatagramAssembly::split( big, 500 + H, true );
  FACT_CHECK( f.size() == 20 + 3 );
  FACT_CHECK( reassembles( f, big ) );
  FACT_CHECK( reassembles( f, big, 0, 8 ) );   /* one from each of two groups */
  FACT_CHECK( reassembles( f, big, 7, 19 ) );
  FACT_CHECK( !reassembles( f, big, 0, 7 ) );  /* two from one group */
  FACT_CHECK( !reassembles( f, big, 16, 19 ) );
  FACT_CHECK( !reassembles( f, big, 3, 20 ) ); /* a piece and its own group's parity */
  FACT_CHECK( reassembles( f, big, 3, 21 ) );  /* a piece and another group's parity */

  /* when parity is worth sending: when the datagram would otherwise be lost 2% of the time or more */
  FACT_CHECK( ( !Gosh::parity_worthwhile( 0.0, 2 ) && !Gosh::parity_worthwhile( 0.0, 200 ) ) );
  FACT_CHECK( ( !Gosh::parity_worthwhile( 0.01, 2 ) && Gosh::parity_worthwhile( 0.01, 3 ) ) );
  FACT_CHECK( ( !Gosh::parity_worthwhile( 0.001, 20 ) && Gosh::parity_worthwhile( 0.001, 21 ) ) );
  FACT_CHECK( ( Gosh::parity_worthwhile( 0.05, 2 ) && Gosh::parity_worthwhile( 1.0, 2 ) ) );

  /*
   * The most pieces a datagram can have is 226, with or without parity,
   * because the receiver keeps indexes free for parity either way.
   * (Found by fuzzing: the sender used to allow up to 255 without
   * parity, which the receiver would then refuse to put together.)
   */
  for ( int with_parity = 0; with_parity < 2; with_parity++ ) {
    const std::string most = varied( 226 * 10 );
    f = Gosh::DatagramAssembly::split( most, 10 + H, with_parity );
    FACT_CHECK( f.size() == 226u + ( with_parity ? 29 : 0 ) );
    FACT_CHECK( reassembles( f, most ) );
    FACT_CHECK( Gosh::DatagramAssembly::split( most + "x", 10 + H, with_parity ).empty() );
    FACT_CHECK( Gosh::DatagramAssembly::piece_count( most.size(), 10 + H ) == 226 );
    FACT_CHECK( Gosh::DatagramAssembly::piece_count( most.size() + 1, 10 + H ) == 0 );
  }

  /* what can't be done */
  FACT_CHECK( Gosh::DatagramAssembly::split( d, H, false ).empty() );
  FACT_CHECK( Gosh::DatagramAssembly::split( varied( 65507 ), 100 + H, false ).empty() ); /* too many fragments */
  FACT_CHECK( Gosh::DatagramAssembly::split( varied( 65507 ), 500 + H, true ).size() == 132 + 17 );

  /* junk, and fragments of something else, are refused without disturbing what is there */
  f = Gosh::DatagramAssembly::split( d, 1000 + H, true );
  const std::vector<std::string> other = Gosh::DatagramAssembly::split( varied( 2400 ), 1000 + H, true );
  Gosh::DatagramAssembly a;
  std::string out;
  bool parity;
  FACT_CHECK( a.empty() );
  FACT_CHECK( a.add( "", out, parity ) == Gosh::DatagramAssembly::REJECTED );
  /* the wrong length for its place */
  FACT_CHECK( a.add( f[0].substr( 0, f[0].size() - 1 ), out, parity ) == Gosh::DatagramAssembly::REJECTED );
  /* an index beyond the pieces and parity */
  FACT_CHECK( a.add( std::string( "\x05\x03\x09\xC4", 4 ) + std::string( 834, 'x' ), out, parity )
         == Gosh::DatagramAssembly::REJECTED );
  /* a count of one: not fragmented at all */
  FACT_CHECK( a.add( std::string( "\x00\x01\x00\x05hello", 9 ), out, parity ) == Gosh::DatagramAssembly::REJECTED );
  FACT_CHECK( a.empty() );
  FACT_CHECK( ( a.add( f[0], out, parity ) == Gosh::DatagramAssembly::INCOMPLETE && !a.empty() ) );
  FACT_CHECK( a.add( other[1], out, parity ) == Gosh::DatagramAssembly::REJECTED );
  FACT_CHECK( a.add( f[0], out, parity ) == Gosh::DatagramAssembly::INCOMPLETE ); /* a duplicate changes nothing */
  FACT_CHECK( a.add( f[2], out, parity ) == Gosh::DatagramAssembly::INCOMPLETE );
  FACT_CHECK( ( a.add( f[3], out, parity ) == Gosh::DatagramAssembly::COMPLETE && out == d && parity ) );
}

/* Joins two meetups, standing in for an established mosh session. */
class TestLink : public SideChannelLink
{
public:
  SideChannelSink* peer;
  bool ready;
  size_t max_payload;
  std::deque<std::string> sent; /* everything that crossed, for replaying */
  int lose;                     /* lose the datagram this many sends from now (0 is the next); -1 for none */
  double loss;                  /* what the peer is said to be missing of what we send */

  TestLink() : peer( NULL ), ready( true ), max_payload( 1200 ), sent(), lose( -1 ), loss( 0 ) {}

  bool sidechannel_ready( void ) const { return ready; }
  size_t sidechannel_max_payload( void ) const { return max_payload; }
  double sidechannel_loss( void ) const { return loss; }
  bool sidechannel_connected( void ) const { return true; }
  void sidechannel_send( const std::string& payload )
  {
    sent.push_back( payload );
    if ( lose >= 0 && lose-- == 0 ) {
      return;
    }
    peer->sidechannel_recv( payload );
  }

private:
  TestLink( const TestLink& );
  TestLink& operator=( const TestLink& );
};

static int udp_socket( uint16_t& port )
{
  Facts* facts = current_facts;
  int fd = socket( AF_INET, SOCK_DGRAM, 0 );
  FACT_CHECK( fd >= 0 );
  struct sockaddr_in sin;
  memset( &sin, 0, sizeof( sin ) );
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  FACT_CHECK( bind( fd, reinterpret_cast<struct sockaddr*>( &sin ), sizeof( sin ) ) == 0 );
  socklen_t len = sizeof( sin );
  FACT_CHECK( getsockname( fd, reinterpret_cast<struct sockaddr*>( &sin ), &len ) == 0 );
  port = ntohs( sin.sin_port );
  return fd;
}

/* Some systems (macOS, the BSDs) won't send a UDP datagram anywhere near the protocol's limit. */
static size_t largest_datagram( void )
{
  Facts* facts = current_facts;
  uint16_t port;
  const int receiver = udp_socket( port );
  const int sender = udp_socket( port );
  struct sockaddr_in to;
  socklen_t len = sizeof( to );
  FACT_CHECK( getsockname( receiver, reinterpret_cast<struct sockaddr*>( &to ), &len ) == 0 );
  const std::string most( 65507, 'x' );
  const bool ok = sendto( sender, most.data(), most.size(), 0, reinterpret_cast<struct sockaddr*>( &to ), len )
                  == static_cast<ssize_t>( most.size() );
  close( sender );
  close( receiver );
  return ok ? most.size() : 9000;
}

static bool readable( int fd, int timeout_ms = 2000 )
{
  struct pollfd p;
  p.fd = fd;
  p.events = POLLIN;
  p.revents = 0;
  return poll( &p, 1, timeout_ms ) == 1;
}

static void send_to_port( int fd, uint16_t port, const std::string& s )
{
  Facts* facts = current_facts;
  struct sockaddr_in sin;
  memset( &sin, 0, sizeof( sin ) );
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  sin.sin_port = htons( port );
  FACT_CHECK( sendto( fd, s.data(), s.size(), 0, reinterpret_cast<struct sockaddr*>( &sin ), sizeof( sin ) )
         == static_cast<ssize_t>( s.size() ) );
}

static std::string receive( int fd, struct sockaddr_in* from = NULL )
{
  Facts* facts = current_facts;
  char buf[65536];
  struct sockaddr_in sin;
  socklen_t len = sizeof( sin );
  ssize_t n = recvfrom( fd, buf, sizeof( buf ), 0, reinterpret_cast<struct sockaddr*>( &sin ), &len );
  FACT_CHECK( n >= 0 );
  if ( from ) {
    *from = sin;
  }
  return std::string( buf, n );
}
FACTS( Relay )
{
  current_facts = facts;
  /* the service being forwarded to */
  uint16_t target_port;
  int target = udp_socket( target_port );

  /* find a free port for the client end to listen on */
  uint16_t listen_port;
  close( udp_socket( listen_port ) );

  char spec[64];
  snprintf( spec, sizeof( spec ), "%d:127.0.0.1:%d", listen_port, target_port );
  std::vector<std::string> specs( 1, spec );

  TestLink to_server, to_client;
  UDPForwarder client( &to_server, UDPForwarder::LISTEN, specs );
  UDPForwarder server( &to_client, UDPForwarder::CONNECT, specs );
  to_server.peer = &server;
  to_client.peer = &client;

  FACT_CHECK( ( client.fds().size() == 1 && server.fds().empty() ) );
  const int listen_fd = client.fds().at( 0 );

  /* two local applications, so two flows */
  uint16_t app_port[2];
  int app[2] = { udp_socket( app_port[0] ), udp_socket( app_port[1] ) };
  struct sockaddr_in server_side[2];

  for ( int i = 0; i < 2; i++ ) {
    const std::string request = i ? "request from app 1" : "request from app 0";
    send_to_port( app[i], listen_port, request );
    FACT_CHECK( readable( listen_fd ) );
    client.read_fd( listen_fd );

    FACT_CHECK( readable( target ) );
    FACT_CHECK( receive( target, &server_side[i] ) == request );
  }
  FACT_CHECK( ( client.flow_count() == 2 && server.flow_count() == 2 ) );
  FACT_CHECK( server_side[0].sin_port != server_side[1].sin_port ); /* each flow has its own socket */

  /* replies go back to the right application, in either order */
  for ( int i = 1; i >= 0; i-- ) {
    const std::string reply = i ? "reply to app 1" : "reply to app 0";
    FACT_CHECK( sendto( target,
                   reply.data(),
                   reply.size(),
                   0,
                   reinterpret_cast<struct sockaddr*>( &server_side[i] ),
                   sizeof( server_side[i] ) )
           == static_cast<ssize_t>( reply.size() ) );

    std::vector<int> fds = server.fds();
    FACT_CHECK( fds.size() == 2 );
    struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { fds[1], POLLIN, 0 } };
    FACT_CHECK( poll( p, 2, 2000 ) == 1 );
    server.read_fd( p[0].revents ? fds[0] : fds[1] );

    FACT_CHECK( readable( app[i] ) );
    struct sockaddr_in from;
    FACT_CHECK( receive( app[i], &from ) == reply );
    FACT_CHECK( ntohs( from.sin_port ) == listen_port ); /* the reply comes from where the request went */
    FACT_CHECK( !readable( app[1 - i], 50 ) );
  }

  /* a replayed datagram is not delivered twice */
  FACT_CHECK( to_server.sent.size() == 2 );
  server.sidechannel_recv( to_server.sent[0] );
  server.sidechannel_recv( to_server.sent[1] );
  FACT_CHECK( !readable( target, 100 ) );

  /* the largest datagram that fits goes through whole */
  const size_t limit = to_server.max_payload - Gosh::FRAME_HEADER_LEN;
  size_t crossed = to_server.sent.size();
  send_to_port( app[0], listen_port, std::string( limit, 'x' ) );
  FACT_CHECK( readable( listen_fd ) );
  client.read_fd( listen_fd );
  FACT_CHECK( readable( target ) );
  FACT_CHECK( receive( target ) == std::string( limit, 'x' ) );
  FACT_CHECK( ( to_server.sent.size() == crossed + 1 && client.get_fragmented() == 0 ) );

  /* one byte more goes in two fragments, and arrives as one datagram */
  crossed = to_server.sent.size();
  send_to_port( app[0], listen_port, std::string( limit + 1, 'y' ) );
  FACT_CHECK( readable( listen_fd ) );
  client.read_fd( listen_fd );
  FACT_CHECK( readable( target ) );
  FACT_CHECK( receive( target ) == std::string( limit + 1, 'y' ) );
  FACT_CHECK( !readable( target, 50 ) );
  FACT_CHECK( ( to_server.sent.size() == crossed + 2 && client.get_fragmented() == 1 ) );
  FACT_CHECK( ( server.get_reassembled() == 1 && server.get_recovered() == 0 && server.partial_count() == 0 ) );

  /* replaying the fragments does not deliver it again */
  server.sidechannel_recv( to_server.sent[crossed] );
  server.sidechannel_recv( to_server.sent[crossed + 1] );
  FACT_CHECK( ( !readable( target, 50 ) && server.partial_count() == 0 ) );

  /* the largest datagram there is (or that this system will send), there and back */
  const std::string huge = varied( largest_datagram() );
  send_to_port( app[0], listen_port, huge );
  FACT_CHECK( readable( listen_fd ) );
  client.read_fd( listen_fd );
  FACT_CHECK( readable( target ) );
  struct sockaddr_in huge_from;
  FACT_CHECK( receive( target, &huge_from ) == huge );
  FACT_CHECK( sendto( target,
                 huge.data(),
                 huge.size(),
                 0,
                 reinterpret_cast<struct sockaddr*>( &huge_from ),
                 sizeof( huge_from ) )
         == static_cast<ssize_t>( huge.size() ) );
  {
    std::vector<int> fds = server.fds();
    struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { fds[1], POLLIN, 0 } };
    FACT_CHECK( poll( p, 2, 2000 ) == 1 );
    server.read_fd( p[0].revents ? fds[0] : fds[1] );
  }
  FACT_CHECK( readable( app[0] ) );
  FACT_CHECK( receive( app[0] ) == huge );
  FACT_CHECK( ( client.get_reassembled() == 1 && client.get_dropped_oversize() == 0 ) );

  /* without parity, a lost fragment loses the datagram... */
  const std::string big = varied( 3000 ); /* three fragments */
  to_server.lose = 1;
  send_to_port( app[0], listen_port, big );
  FACT_CHECK( readable( listen_fd ) );
  client.read_fd( listen_fd );
  FACT_CHECK( !readable( target, 100 ) );
  FACT_CHECK( ( server.partial_count() == 1 && server.get_abandoned() == 0 ) );

  /* ...and what did arrive is forgotten after a while */
  usleep( ( UDPForwarder::REASSEMBLY_TIMEOUT + 100 ) * 1000 );
  freeze_timestamp();
  send_to_port( app[0], listen_port, big );
  FACT_CHECK( readable( listen_fd ) );
  client.read_fd( listen_fd );
  FACT_CHECK( readable( target ) );
  FACT_CHECK( receive( target ) == big );
  FACT_CHECK( ( server.partial_count() == 0 && server.get_abandoned() == 1 ) );

  /* with parity there is one more fragment, and losing any one of them does no harm */
  client.set_parity_mode( UDPForwarder::PARITY_ALWAYS );
  for ( int lost = 0; lost < 4; lost++ ) {
    const uint64_t recovered_before = server.get_recovered();
    crossed = to_server.sent.size();
    to_server.lose = lost;
    send_to_port( app[0], listen_port, big );
    FACT_CHECK( readable( listen_fd ) );
    client.read_fd( listen_fd );
    FACT_CHECK( to_server.sent.size() == crossed + 4 );
    FACT_CHECK( readable( target ) );
    FACT_CHECK( receive( target ) == big );
    FACT_CHECK( !readable( target, 20 ) );
    /* parity was needed unless it was the parity that got lost */
    FACT_CHECK( server.get_recovered() == recovered_before + ( lost < 3 ? 1 : 0 ) );
    FACT_CHECK( server.partial_count() == 0 );
  }

  /* losing two is still too many */
  to_server.lose = 0;
  send_to_port( app[0], listen_port, big );
  FACT_CHECK( readable( listen_fd ) );
  to_server.sent.clear();
  client.read_fd( listen_fd ); /* the first fragment is lost here... */
  FACT_CHECK( to_server.sent.size() == 4 );
  FACT_CHECK( readable( target ) ); /* ...so parity rebuilt it */
  FACT_CHECK( receive( target ) == big );
  UDPForwarder lonely( &to_client, UDPForwarder::CONNECT, specs ); /* a receiver that sees only half of them */
  lonely.sidechannel_recv( to_server.sent[1] );
  lonely.sidechannel_recv( to_server.sent[3] );
  FACT_CHECK( ( lonely.partial_count() == 1 && lonely.get_reassembled() == 0 ) );
  lonely.sidechannel_recv( to_server.sent[2] ); /* a third makes it whole */
  FACT_CHECK( ( lonely.partial_count() == 0 && lonely.get_reassembled() == 1 && lonely.get_recovered() == 1 ) );
  FACT_CHECK( readable( target ) );
  FACT_CHECK( receive( target ) == big );

  /*
   * Left to decide for itself, the sender goes by how much the session
   * is losing and how many fragments the datagram needs.
   */
  client.set_parity_mode( UDPForwarder::PARITY_AUTO );
  const std::string two = varied( 2000 ); /* two fragments; "big" is three */
  const struct
  {
    double loss;
    const std::string* datagram;
    bool parity;
  } cases[] = { { 0.0, &big, false },  { 0.0, &huge, false }, /* a clean path: never */
                { 0.01, &two, false }, { 0.01, &big, true },  /* 1%: from three fragments up */
                { 0.05, &two, true },  { 0.001, &big, false },
                { 0.001, &huge, huge.size() > 21 * 1200 } }; /* 0.1%: from 21 fragments up */
  for ( size_t i = 0; i < sizeof( cases ) / sizeof( cases[0] ); i++ ) {
    const uint64_t parity_before = client.get_with_parity();
    to_server.loss = cases[i].loss;
    send_to_port( app[0], listen_port, *cases[i].datagram );
    FACT_CHECK( readable( listen_fd ) );
    client.read_fd( listen_fd );
    FACT_CHECK( ( readable( target ) && receive( target ) == *cases[i].datagram ) );
    FACT_CHECK( client.get_with_parity() == parity_before + ( cases[i].parity ? 1 : 0 ) );
  }

  /* and small datagrams are sent whole as ever */
  crossed = to_server.sent.size();
  send_to_port( app[0], listen_port, "small" );
  FACT_CHECK( readable( listen_fd ) );
  client.read_fd( listen_fd );
  FACT_CHECK( ( readable( target ) && receive( target ) == "small" ) );
  FACT_CHECK( to_server.sent.size() == crossed + 1 );
  to_server.loss = 0;

  client.set_parity_mode( UDPForwarder::PARITY_NEVER );

  /* nothing is sent until the peer is known to support forwarding */
  to_server.ready = false;
  const size_t sent_before = to_server.sent.size();
  send_to_port( app[0], listen_port, "too early" );
  FACT_CHECK( readable( listen_fd ) );
  client.read_fd( listen_fd );
  FACT_CHECK( to_server.sent.size() == sent_before );
  FACT_CHECK( !readable( target, 100 ) );

  /* junk from the network is ignored */
  server.sidechannel_recv( "" );
  server.sidechannel_recv( std::string( 1, static_cast<char>( SIDECHANNEL_MAGIC ) ) );
  server.sidechannel_recv( encode_data( 9, 0, 1000, "no such forward" ) );
  client.sidechannel_recv( encode_data( 0, 999, 1000, "no such flow" ) );
  FACT_CHECK( ( client.flow_count() == 2 && server.flow_count() == 2 ) );

  close( target );
  close( app[0] );
  close( app[1] );
}
FACTS( Remote )
{
  current_facts = facts;
  uint16_t target_port;
  const int target = udp_socket( target_port );
  uint16_t local_port, remote_port;
  close( udp_socket( local_port ) );
  close( udp_socket( remote_port ) );

  char spec[64];
  snprintf( spec, sizeof( spec ), "%d:127.0.0.1:%d", local_port, target_port );
  const std::vector<std::string> local_specs( 1, spec );
  snprintf( spec, sizeof( spec ), "%d:127.0.0.1:%d", remote_port, target_port );
  const std::vector<std::string> remote_specs( 1, spec );
  const std::vector<std::string> none;

  /* each end has both kinds, sharing one side channel */
  TestLink to_server, to_client;
  Forwarding client( &to_server, true, local_specs, none, none, remote_specs );
  Forwarding server( &to_client, false, local_specs, none, none, remote_specs );
  to_server.peer = server.sink();
  to_client.peer = client.sink();

  /* the client listens for the local forward and the server for the remote one */
  FACT_CHECK( ( client.fds().size() == 1 && server.fds().size() == 1 ) );

  uint16_t app_port;
  const int app = udp_socket( app_port );

  for ( int remote = 1; remote >= 0; remote-- ) {
    Forwarding& listener = remote ? server : client;
    Forwarding& relay = remote ? client : server;
    TestLink& outward = remote ? to_client : to_server;
    const int listen_fd = listener.fds().at( 0 );

    for ( int large = 0; large < 2; large++ ) {
      const std::string request
        = large ? varied( 5000 ) : std::string( remote ? "to the remote port" : "to the local port" );
      const size_t crossed = outward.sent.size();
      send_to_port( app, remote ? remote_port : local_port, request );
      FACT_CHECK( readable( listen_fd ) );
      listener.read_fd( listen_fd );
      FACT_CHECK( outward.sent.size() == crossed + ( large ? 5 : 1 ) );
      FACT_CHECK( readable( target ) );
      struct sockaddr_in from;
      FACT_CHECK( receive( target, &from ) == request );

      /* and back again */
      const std::string reply = large ? varied( 4000 ) : std::string( "reply" );
      FACT_CHECK( sendto( target,
                     reply.data(),
                     reply.size(),
                     0,
                     reinterpret_cast<struct sockaddr*>( &from ),
                     sizeof( from ) )
             == static_cast<ssize_t>( reply.size() ) );
      const std::vector<int> fds = relay.fds();
      std::vector<struct pollfd> polls( fds.size() );
      for ( size_t i = 0; i < fds.size(); i++ ) {
        polls[i].fd = fds[i];
        polls[i].events = POLLIN;
        polls[i].revents = 0;
      }
      FACT_CHECK( poll( polls.data(), polls.size(), 2000 ) == 1 );
      for ( size_t i = 0; i < fds.size(); i++ ) {
        if ( polls[i].revents ) {
          relay.read_fd( fds[i] );
        }
      }
      FACT_CHECK( readable( app ) );
      struct sockaddr_in back;
      FACT_CHECK( receive( app, &back ) == reply );
      FACT_CHECK( ntohs( back.sin_port ) == ( remote ? remote_port : local_port ) );
    }
  }
  FACT_CHECK( ( !readable( target, 50 ) && !readable( app, 50 ) ) );

  close( target );
  close( app );
}
FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Spec );
  FACTS_REGISTER( Wire );
  FACTS_REGISTER( Replay );
  FACTS_REGISTER( Fragments );
  FACTS_REGISTER( Relay );
  FACTS_REGISTER( Remote );
}

FACTS_MAIN
