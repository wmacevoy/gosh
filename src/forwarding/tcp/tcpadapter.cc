/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/forwarding/tcp/tcpadapter.h"
#include "src/util/timestamp.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace Network;

static void prepare_socket( int fd )
{
  int flags = fcntl( fd, F_GETFL );
  if ( flags >= 0 ) {
    fcntl( fd, F_SETFL, flags | O_NONBLOCK );
  }
  flags = fcntl( fd, F_GETFD );
  if ( flags >= 0 ) {
    fcntl( fd, F_SETFD, flags | FD_CLOEXEC );
  }
#ifdef SO_NOSIGPIPE
  int on = 1;
  setsockopt( fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof( on ) );
#endif
}

static void no_delay( int fd )
{
  int on = 1;
  setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof( on ) );
}

/* Close so that the application sees a reset rather than an orderly end. */
static void close_with_reset( int fd )
{
  struct linger l;
  l.l_onoff = 1;
  l.l_linger = 0;
  setsockopt( fd, SOL_SOCKET, SO_LINGER, &l, sizeof( l ) );
  ::close( fd );
}

AdapterPort::AdapterPort( SegmentSink* s_sink, bool s_client )
  : listeners(), edge( s_sink ), logic( s_client )
{}

AdapterPort::Edge::Edge( SegmentSink* s_sink )
  : sink( s_sink ), targets(), sockets(), prng(), counted( false ), next_sequence( 0 )
{}

AdapterPort::~AdapterPort()
{
  for ( std::vector<Listener>::iterator i = listeners.begin(); i != listeners.end(); i++ ) {
    ::close( i->fd );
  }
  for ( std::map<Id, int>::iterator i = edge.sockets.begin(); i != edge.sockets.end(); i++ ) {
    ::close( i->second );
  }
}

std::string AdapterPort::add_listener( uint8_t forward, const Gosh::ForwardSpec& spec )
{
  const bool wildcard = spec.bind_host.empty() || spec.bind_host == "*";
  const std::string where = spec.bind_host + ":" + spec.bind_port;

  struct addrinfo hints, *res = NULL;
  memset( &hints, 0, sizeof( hints ) );
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  int errcode = getaddrinfo( wildcard ? NULL : spec.bind_host.c_str(), spec.bind_port.c_str(), &hints, &res );
  if ( errcode != 0 ) {
    throw NetworkException( "TCP forwarding: " + where + ": " + gai_strerror( errcode ), 0 );
  }

  int fd = socket( res->ai_family, SOCK_STREAM, 0 );
  int saved_errno = errno;
  const char* failed = "socket";
  if ( fd >= 0 ) {
    int on = 1;
    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof( on ) );
    prepare_socket( fd );
    if ( bind( fd, res->ai_addr, res->ai_addrlen ) < 0 ) {
      failed = "bind";
    } else if ( listen( fd, 16 ) < 0 ) {
      failed = "listen";
    } else {
      failed = NULL;
    }
    saved_errno = errno;
  }
  freeaddrinfo( res );

  if ( failed ) {
    if ( fd >= 0 ) {
      ::close( fd );
    }
    throw NetworkException( std::string( "TCP forwarding: " ) + failed + " " + where, saved_errno );
  }

  Listener listener;
  listener.forward = forward;
  listener.fd = fd;
  listeners.push_back( listener );
  return where;
}

void AdapterPort::add_target( uint8_t forward, const Addr& addr, socklen_t len )
{
  if ( edge.targets.size() <= forward ) {
    edge.targets.resize( forward + 1 );
  }
  edge.targets[forward].addr = addr;
  edge.targets[forward].len = len;
  edge.targets[forward].present = true;
}

void AdapterPort::add_spawner( uint8_t forward, const std::function<int()>& spawn )
{
  if ( edge.targets.size() <= forward ) {
    edge.targets.resize( forward + 1 );
  }
  edge.targets[forward].spawn = spawn;
  edge.targets[forward].present = true;
}

Gosh::Link AdapterPort::link( void ) const
{
  Gosh::Link l;
  l.ready = edge.sink->segment_ready();
  l.in_contact = edge.sink->link_up();
  l.max_datagram = edge.sink->max_datagram();
  return l;
}

int AdapterPort::Edge::socket_of( Id id ) const
{
  const std::map<Id, int>::const_iterator i = sockets.find( id );
  return i == sockets.end() ? -1 : i->second;
}

bool AdapterPort::open_with( uint8_t forward, int fd, uint64_t now )
{
  Id id = 0;
  if ( !logic.opened( forward, now, link(), edge, id ) ) {
    ::close( fd );
    return false;
  }
  prepare_socket( fd );
  edge.sockets[id] = fd;
  return true;
}

/* Listening end: an application has connected. Open a connection for it through the tunnel. */
void AdapterPort::accept_from( const Listener& listener, uint64_t now )
{
  int fd = accept( listener.fd, NULL, NULL );
  if ( fd < 0 ) {
    return;
  }

  Id id = 0;
  if ( !logic.opened( listener.forward, now, link(), edge, id ) ) {
    /* no forwarding at the other end (yet), or too busy: connection refused */
    close_with_reset( fd );
    return;
  }
  prepare_socket( fd );
  no_delay( fd );
  edge.sockets[id] = fd;
}

void AdapterPort::from_network( uint8_t forward, const TCPInUDP& segment )
{
  logic.segment( forward, segment, timestamp(), link(), edge );
}

std::vector<int> AdapterPort::fds( void ) const
{
  std::vector<int> ret;
  for ( std::vector<Listener>::const_iterator i = listeners.begin(); i != listeners.end(); i++ ) {
    ret.push_back( i->fd );
  }
  /* only those the logic has room to read from */
  const std::vector<Id> readable = logic.readable();
  for ( std::vector<Id>::const_iterator i = readable.begin(); i != readable.end(); i++ ) {
    ret.push_back( edge.socket_of( *i ) );
  }
  return ret;
}

void AdapterPort::read_fd( int fd )
{
  const uint64_t now = timestamp();

  for ( std::vector<Listener>::const_iterator i = listeners.begin(); i != listeners.end(); i++ ) {
    if ( i->fd == fd ) {
      accept_from( *i, now );
      return;
    }
  }

  for ( std::map<Id, int>::const_iterator i = edge.sockets.begin(); i != edge.sockets.end(); i++ ) {
    if ( i->second != fd ) {
      continue;
    }
    const Id id = i->first;
    const size_t want = logic.wants( id );
    if ( !want ) {
      return;
    }
    char buf[Logic::READ_AT_ONCE];
    const ssize_t n = recv( fd, buf, std::min( want, sizeof( buf ) ), MSG_DONTWAIT );
    if ( n >= 0 ) {
      logic.read( id, buf, n, now, link(), edge ); /* (0: the application has finished writing) */
    } else if ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) {
      /* nothing there after all */
    } else {
      logic.failed( id, link(), edge );
    }
    return;
  }
}

void AdapterPort::tick( uint64_t now )
{
  logic.tick( now, link(), edge );
}

int AdapterPort::wait_time( uint64_t now ) const
{
  return logic.wait_time( now );
}

/* ---- what the logic asks for */

void AdapterPort::Edge::send( uint8_t forward, const TCPInUDP& segment )
{
  sink->to_network( forward, segment );
}

uint32_t AdapterPort::Edge::fresh_sequence( void )
{
  return counted ? next_sequence++ : prng.uint32();
}

/* Connecting end: the other end wants a connection. See whether the target will have it. */
bool AdapterPort::Edge::connect( Id id, uint8_t forward )
{
  if ( forward >= targets.size() || !targets[forward].present ) {
    return false;
  }
  const Target& target = targets[forward];

  int fd = target.spawn ? target.spawn() : socket( target.addr.sa.sa_family, SOCK_STREAM, 0 );
  if ( fd < 0 ) {
    return false;
  }
  prepare_socket( fd );

  if ( !target.spawn ) {
    no_delay( fd );
    if ( ::connect( fd, &target.addr.sa, target.len ) < 0 && errno != EINPROGRESS ) {
      ::close( fd );
      return false;
    }
  }
  /* (a socket that came connected is found to be so on the next tick, like any other) */

  sockets[id] = fd;
  return true;
}

int AdapterPort::Edge::connecting( Id id )
{
  struct pollfd p;
  p.fd = socket_of( id );
  p.events = POLLOUT;
  p.revents = 0;
  if ( poll( &p, 1, 0 ) <= 0 ) {
    return 0;
  }
  int error = 0;
  socklen_t len = sizeof( error );
  return ( getsockopt( p.fd, SOL_SOCKET, SO_ERROR, &error, &len ) == 0 && error == 0 ) ? 1 : -1;
}

long AdapterPort::Edge::deliver( Id id, const char* data, size_t length )
{
  const ssize_t n = ::send( socket_of( id ), data, length, MSG_DONTWAIT | MSG_NOSIGNAL );
  if ( n > 0 ) {
    return n;
  }
  if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) ) {
    return 0; /* the application is slow */
  }
  return -1; /* the application has gone */
}

void AdapterPort::Edge::shut_write( Id id )
{
  shutdown( socket_of( id ), SHUT_WR );
}

void AdapterPort::Edge::close( Id id, bool reset )
{
  const std::map<Id, int>::iterator i = sockets.find( id );
  if ( i == sockets.end() ) {
    return;
  }
  if ( reset ) {
    close_with_reset( i->second );
  } else {
    ::close( i->second );
  }
  sockets.erase( i );
}
