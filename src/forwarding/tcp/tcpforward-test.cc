/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* Tests TCP forwarding: real TCP connections through a pair of
   forwarders joined by an in-memory side channel that can lose
   datagrams. */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/forwarding/forwarding.h"
#include "src/forwarding/tcp/tcpadapter.h"
#include "src/forwarding/tcp/tcpforward.h"
#include "src/util/timestamp.h"
#include "vendor/facts/facts.h"

using namespace Network;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif


/* One direction of an established mosh session.  Delivery is deferred, as it is over a network. */
class TestLink : public SideChannelLink
{
public:
  std::deque<std::string> queue;
  bool ready;
  size_t max_payload;
  unsigned int lose_one_in; /* 0 for none */
  unsigned int sent, lost;
  uint32_t random_state;

  TestLink()
    : queue(), ready( true ), max_payload( 1200 ), lose_one_in( 0 ), sent( 0 ), lost( 0 ), random_state( 1 )
  {}

  bool sidechannel_ready( void ) const { return ready; }
  size_t sidechannel_max_payload( void ) const { return max_payload; }
  double sidechannel_loss( void ) const { return 0; }
  bool sidechannel_connected( void ) const { return true; }
  void sidechannel_send( const std::string& payload )
  {
    Facts* facts = FactsCurrent(); /* the group running */
    FACT_CHECK( payload.size() <= max_payload );
    sent++;
    random_state = random_state * 1103515245 + 12345;
    if ( lose_one_in && ( random_state >> 16 ) % lose_one_in == 0 ) {
      lost++;
      return;
    }
    queue.push_back( payload );
  }

  void deliver_to( SideChannelSink& sink )
  {
    while ( !queue.empty() ) {
      const std::string payload = queue.front();
      queue.pop_front();
      sink.sidechannel_recv( payload );
    }
  }
};

static int listening_socket( uint16_t& port )
{
  Facts* facts = FactsCurrent(); /* the group running */
  int fd = socket( AF_INET, SOCK_STREAM, 0 );
  FACT_CHECK( fd >= 0 );
  struct sockaddr_in sin;
  memset( &sin, 0, sizeof( sin ) );
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  FACT_CHECK( bind( fd, reinterpret_cast<struct sockaddr*>( &sin ), sizeof( sin ) ) == 0 );
  FACT_CHECK( listen( fd, 8 ) == 0 );
  socklen_t len = sizeof( sin );
  FACT_CHECK( getsockname( fd, reinterpret_cast<struct sockaddr*>( &sin ), &len ) == 0 );
  port = ntohs( sin.sin_port );
  return fd;
}

static uint16_t unused_port( void )
{
  uint16_t port;
  close( listening_socket( port ) );
  return port;
}

static char pattern( size_t i )
{
  return static_cast<char>( ( i * 2654435761u ) >> 13 );
}

static bool write_all( int fd, const std::string& s )
{
  size_t done = 0;
  while ( done < s.size() ) {
    ssize_t n = send( fd, s.data() + done, s.size() - done, MSG_NOSIGNAL );
    if ( n <= 0 ) {
      return false;
    }
    done += n;
  }
  return true;
}

static std::string read_to_end( int fd )
{
  std::string ret;
  char buf[65536];
  ssize_t n;
  while ( ( n = recv( fd, buf, sizeof( buf ), 0 ) ) > 0 ) {
    ret.append( buf, n );
  }
  return ret;
}

/* The service being forwarded to: reads a whole stream, then answers with its complement. */
static pid_t start_target( int listener )
{
  Facts* facts = FactsCurrent(); /* the group running */
  pid_t pid = fork();
  FACT_CHECK( pid >= 0 );
  if ( pid == 0 ) {
    alarm( 120 );
    while ( true ) {
      int fd = accept( listener, NULL, NULL );
      if ( fd < 0 ) {
        _exit( 1 );
      }
      std::string data = read_to_end( fd );
      for ( size_t i = 0; i < data.size(); i++ ) {
        data[i] = ~data[i];
      }
      write_all( fd, data );
      close( fd );
    }
  }
  return pid;
}

enum Outcome
{
  DELIVERED = 10, /* everything went there and came back intact */
  CORRUPTED,
  REFUSED, /* could not connect, or the connection was closed or reset with nothing received */
};

/* The local application: sends a stream, closes its side, and checks what comes back. */
static pid_t start_application( uint16_t port, size_t size )
{
  Facts* facts = FactsCurrent(); /* the group running */
  pid_t pid = fork();
  FACT_CHECK( pid >= 0 );
  if ( pid == 0 ) {
    alarm( 120 );
    int fd = socket( AF_INET, SOCK_STREAM, 0 );
    struct sockaddr_in sin;
    memset( &sin, 0, sizeof( sin ) );
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    sin.sin_port = htons( port );
    if ( connect( fd, reinterpret_cast<struct sockaddr*>( &sin ), sizeof( sin ) ) < 0 ) {
      _exit( REFUSED );
    }

    std::string data( size, 0 );
    for ( size_t i = 0; i < size; i++ ) {
      data[i] = pattern( i );
    }
    const bool wrote = write_all( fd, data );
    shutdown( fd, SHUT_WR );
    const std::string back = read_to_end( fd );
    if ( back.empty() ) {
      _exit( REFUSED );
    }
    if ( !wrote || back.size() != size ) {
      _exit( CORRUPTED );
    }
    for ( size_t i = 0; i < size; i++ ) {
      if ( back[i] != static_cast<char>( ~pattern( i ) ) ) {
        _exit( CORRUPTED );
      }
    }
    _exit( DELIVERED );
  }
  return pid;
}

static SideChannelSink& sink_of( TCPForwarder& f )
{
  return f;
}

static SideChannelSink& sink_of( Forwarding& f )
{
  return *f.sink();
}

/* Runs both ends' event loops until the application exits; returns its exit status. */
template<class End>
static int pump( End& client, TestLink& to_server, End& server, TestLink& to_client, pid_t app )
{
  Facts* facts = FactsCurrent(); /* the group running */
  for ( int rounds = 0; rounds < 200000; rounds++ ) {
    freeze_timestamp();

    std::vector<int> fds = client.fds();
    const size_t client_fds = fds.size();
    const std::vector<int> more = server.fds();
    fds.insert( fds.end(), more.begin(), more.end() );

    std::vector<struct pollfd> polls( fds.size() );
    for ( size_t i = 0; i < fds.size(); i++ ) {
      polls[i].fd = fds[i];
      polls[i].events = POLLIN;
      polls[i].revents = 0;
    }
    const bool traffic = !to_server.queue.empty() || !to_client.queue.empty();
    const int wait = traffic ? 0 : std::min( 20, std::min( client.wait_time(), server.wait_time() ) );
    poll( polls.data(), polls.size(), wait );
    freeze_timestamp();

    for ( size_t i = 0; i < polls.size(); i++ ) {
      if ( polls[i].revents ) {
        ( i < client_fds ? client : server ).read_fd( polls[i].fd );
      }
    }
    client.tick();
    server.tick();
    to_server.deliver_to( sink_of( server ) );
    to_client.deliver_to( sink_of( client ) );

    int status;
    if ( waitpid( app, &status, WNOHANG ) == app ) {
      FACT_CHECK( WIFEXITED( status ) );
      return WEXITSTATUS( status );
    }
  }
  FACT_CHECK( !"transfer did not finish" );
  return -1;
}
FACTS( Transfers )
{
  uint16_t target_port;
  const int listener = listening_socket( target_port );
  const pid_t target = start_target( listener );
  close( listener );

  const uint16_t listen_port = unused_port(), refused_listen_port = unused_port(), dead_port = unused_port();
  char spec[2][64];
  snprintf( spec[0], sizeof( spec[0] ), "%d:127.0.0.1:%d", listen_port, target_port );
  snprintf( spec[1], sizeof( spec[1] ), "%d:127.0.0.1:%d", refused_listen_port, dead_port );
  const std::vector<std::string> specs( spec, spec + 2 );

  TestLink to_server, to_client;
  TCPForwarder client( &to_server, TCPForwarder::LISTEN, specs );
  TCPForwarder server( &to_client, TCPForwarder::CONNECT, specs );

  /* a clean path */
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( listen_port, 300000 ) ) == DELIVERED );
  FACT_CHECK( ( to_server.lost == 0 && to_client.lost == 0 ) );

  /* nothing at all to send is still a connection, opened and closed in order */
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( listen_port, 1 ) ) == DELIVERED );

  /* one datagram in twenty lost, each way: slower, but every byte still arrives, once and in order */
  to_server.lose_one_in = to_client.lose_one_in = 20;
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( listen_port, 300000 ) ) == DELIVERED );
  FACT_CHECK( ( to_server.lost > 5 && to_client.lost > 5 ) );
  to_server.lose_one_in = to_client.lose_one_in = 0;

  /* a small MTU means small segments, never oversize ones (the link checks) */
  to_server.max_payload = to_client.max_payload = 300;
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( listen_port, 50000 ) ) == DELIVERED );
  FACT_CHECK( ( client.get_dropped_oversize() == 0 && server.get_dropped_oversize() == 0 ) );
  to_server.max_payload = to_client.max_payload = 1200;

  /* the target refuses, so the application is refused */
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( refused_listen_port, 1000 ) ) == REFUSED );

  /* no forwarding at the other end (an older server, say): refused at once */
  to_server.ready = false;
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( listen_port, 1000 ) ) == REFUSED );
  to_server.ready = true;

  /* and it all still works afterwards */
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( listen_port, 10000 ) ) == DELIVERED );

  kill( target, SIGTERM );
  waitpid( target, NULL, 0 );
}
FACTS( Remote )
{
  uint16_t target_port;
  const int listener = listening_socket( target_port );
  const pid_t target = start_target( listener );
  close( listener );

  const uint16_t local_port = unused_port(), remote_port = unused_port();
  char spec[64];
  snprintf( spec, sizeof( spec ), "%d:127.0.0.1:%d", local_port, target_port );
  const std::vector<std::string> local_specs( 1, spec );
  snprintf( spec, sizeof( spec ), "%d:127.0.0.1:%d", remote_port, target_port );
  const std::vector<std::string> remote_specs( 1, spec );
  const std::vector<std::string> none;

  /* each end has both kinds, sharing one side channel */
  TestLink to_server, to_client;
  Forwarding client( &to_server, true, none, local_specs, remote_specs );
  Forwarding server( &to_client, false, none, local_specs, remote_specs );

  /* the client listens for the local forward and the server for the remote one, not the other way about */
  FACT_CHECK( ( client.fds().size() == 1 && server.fds().size() == 1 ) );

  FACT_CHECK( pump( client, to_server, server, to_client, start_application( remote_port, 200000 ) ) == DELIVERED );
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( local_port, 200000 ) ) == DELIVERED );

  /* both at once, with loss: the two directions' segments must not be confused */
  to_server.lose_one_in = to_client.lose_one_in = 25;
  const pid_t other = start_application( local_port, 100000 );
  FACT_CHECK( pump( client, to_server, server, to_client, start_application( remote_port, 100000 ) ) == DELIVERED );
  FACT_CHECK( pump( client, to_server, server, to_client, other ) == DELIVERED );
  FACT_CHECK( ( to_server.lost > 0 && to_client.lost > 0 ) );

  kill( target, SIGTERM );
  waitpid( target, NULL, 0 );
}

/* The far end of the tunnel, played by the test: it just notes what the adapter sends. */
class RecordingSink : public SegmentSink
{
public:
  std::vector<TCPInUDP> sent;
  bool up;

  RecordingSink() : sent(), up( true ) {}

  bool segment_ready( void ) const { return true; }
  bool link_up( void ) const { return up; }
  size_t max_datagram( void ) const { return 1200; }
  void to_network( uint8_t, const TCPInUDP& segment ) { sent.push_back( segment ); }
};
FACTS( TinyMss )
{
  uint16_t target_port;
  const int listener = listening_socket( target_port );
  Addr target;
  memset( &target, 0, sizeof( target ) );
  target.sin.sin_family = AF_INET;
  target.sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  target.sin.sin_port = htons( target_port );

  RecordingSink sink;
  AdapterPort adapter( &sink, false );
  adapter.add_target( 0, target, sizeof( target.sin ) );

  /* open a connection, asking for a maximum segment size of zero */
  TCPInUDP syn;
  syn.source_port = 1;
  syn.flags = TCPInUDP::SYN;
  syn.seq = 100;
  syn.window = 0xFFFF;
  syn.options = std::string( "\x02\x04\x00\x00", 4 );
  freeze_timestamp();
  adapter.from_network( 0, syn );
  for ( int i = 0; i < 200 && sink.sent.empty(); i++ ) {
    usleep( 10000 );
    freeze_timestamp();
    adapter.tick( frozen_timestamp() );
  }
  FACT_CHECK( ( sink.sent.size() == 1 && sink.sent[0].has( TCPInUDP::SYN ) && sink.sent[0].has( TCPInUDP::ACK ) ) );

  TCPInUDP ack;
  ack.source_port = 1;
  ack.flags = TCPInUDP::ACK;
  ack.seq = 101;
  ack.ack = sink.sent[0].seq + 1;
  ack.window = 0xFFFF;
  adapter.from_network( 0, ack );

  /* the target has plenty to say */
  const int app = accept( listener, NULL, NULL );
  FACT_CHECK( app >= 0 );
  FACT_CHECK( write_all( app, std::string( 1000, 'x' ) ) );
  usleep( 50000 );
  sink.sent.clear();
  const std::vector<int> fds = adapter.fds();
  FACT_CHECK( fds.size() == 1 );
  adapter.read_fd( fds[0] ); /* this used never to return */

  /* it is sent in pieces that are small, but not nothing */
  FACT_CHECK( !sink.sent.empty() );
  for ( size_t i = 0; i < sink.sent.size(); i++ ) {
    FACT_CHECK( ( !sink.sent[i].payload.empty() && sink.sent[i].payload.size() <= AdapterPort::MIN_MSS ) );
  }

  close( app );
  close( listener );
}

/* Opens a connection through an adapter at the connecting end; returns the target's end of it. */
static int open_through( AdapterPort& adapter, RecordingSink& sink, int listener )
{
  Facts* facts = FactsCurrent(); /* the group running */
  TCPInUDP syn;
  syn.source_port = 1;
  syn.flags = TCPInUDP::SYN;
  syn.seq = 100;
  syn.window = 0xFFFF;
  freeze_timestamp();
  adapter.from_network( 0, syn );
  for ( int i = 0; i < 200 && sink.sent.empty(); i++ ) {
    usleep( 10000 );
    freeze_timestamp();
    adapter.tick( frozen_timestamp() );
  }
  FACT_CHECK( ( sink.sent.size() == 1 && sink.sent[0].has( TCPInUDP::SYN ) ) );

  TCPInUDP ack;
  ack.source_port = 1;
  ack.flags = TCPInUDP::ACK;
  ack.seq = 101;
  ack.ack = sink.sent[0].seq + 1;
  ack.window = 0xFFFF;
  adapter.from_network( 0, ack );

  const int app = accept( listener, NULL, NULL );
  FACT_CHECK( app >= 0 );
  return app;
}

static bool any_reset( const RecordingSink& sink )
{
  for ( size_t i = 0; i < sink.sent.size(); i++ ) {
    if ( sink.sent[i].has( TCPInUDP::RST ) ) {
      return true;
    }
  }
  return false;
}
FACTS( Outage )
{
  uint16_t target_port;
  const int listener = listening_socket( target_port );
  Addr target;
  memset( &target, 0, sizeof( target ) );
  target.sin.sin_family = AF_INET;
  target.sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  target.sin.sin_port = htons( target_port );

  for ( int in_contact = 1; in_contact >= 0; in_contact-- ) {
    RecordingSink sink;
    AdapterPort adapter( &sink, false );
    adapter.add_target( 0, target, sizeof( target.sin ) );
    const int app = open_through( adapter, sink, listener );

    /* data is sent, and nothing comes back */
    FACT_CHECK( write_all( app, "is anybody there?" ) );
    usleep( 50000 );
    sink.sent.clear();
    adapter.read_fd( adapter.fds().at( 0 ) );
    FACT_CHECK( ( sink.sent.size() == 1 && sink.sent[0].payload == "is anybody there?" ) );
    const TCPInUDP data = sink.sent[0];

    sink.up = in_contact;
    sink.sent.clear();
    uint64_t now = frozen_timestamp();
    for ( int minute = 0; minute < 6 * 60; minute++ ) { /* six hours pass */
      now += 60000;
      adapter.tick( now );
    }

    if ( in_contact ) {
      /* silence from a peer we can reach: after some retransmissions, give up and say so */
      FACT_CHECK( ( sink.sent.size() > 3 && sink.sent.size() < 20 ) );
      FACT_CHECK( ( any_reset( sink ) && adapter.connection_count() == 0 ) );
    } else {
      /* silence because the session is out of contact: keep trying, quietly, for as long as it takes */
      FACT_CHECK( ( !any_reset( sink ) && adapter.connection_count() == 1 ) );
      for ( size_t i = 0; i < sink.sent.size(); i++ ) {
        FACT_CHECK( ( sink.sent[i].payload == data.payload && sink.sent[i].seq == data.seq ) );
      }

      /* contact returns: it is sent again at once, not when a backed-off timer gets round to it */
      sink.up = true;
      sink.sent.clear();
      adapter.tick( now + 1 );
      FACT_CHECK( ( sink.sent.size() == 1 && sink.sent[0].payload == data.payload ) );

      /* and the connection carries on as if nothing had happened */
      TCPInUDP reply;
      reply.source_port = 1;
      reply.flags = TCPInUDP::ACK | TCPInUDP::PSH;
      reply.seq = 101;
      reply.ack = data.seq + data.payload.size();
      reply.window = 0xFFFF;
      reply.payload = "yes, still here";
      adapter.from_network( 0, reply );
      char buf[64];
      FACT_CHECK( ( recv( app, buf, sizeof( buf ), 0 ) == 15 && memcmp( buf, "yes, still here", 15 ) == 0 ) );
      FACT_CHECK( adapter.connection_count() == 1 );
    }
    close( app );
  }
  close( listener );
}
FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( TinyMss );
  FACTS_REGISTER( Outage );
  FACTS_REGISTER( Transfers );
  FACTS_REGISTER( Remote );
}

FACTS_MAIN
