/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/crypto/byteorder.h"
#include "src/forwarding/udp/udpforward.h"
#include "src/util/timestamp.h"

using namespace Network;
using namespace Gosh;

static void set_cloexec( int fd )
{
  int flags = fcntl( fd, F_GETFD );
  if ( flags >= 0 ) {
    fcntl( fd, F_SETFD, flags | FD_CLOEXEC );
  }
}

namespace {
class Resolved
{
public:
  struct addrinfo* res;

  Resolved( const std::string& what, const char* node, const char* service, int flags ) : res( NULL )
  {
    struct addrinfo hints;
    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = flags;

    int errcode = getaddrinfo( node, service, &hints, &res );
    if ( errcode != 0 ) {
      throw NetworkException( "UDP forwarding: " + what + ": " + gai_strerror( errcode ), 0 );
    }
  }
  ~Resolved() { freeaddrinfo( res ); }

private:
  Resolved( const Resolved& );
  Resolved& operator=( const Resolved& );
};
}

UDPForwarder::UDPForwarder( SideChannelLink* s_link,
                            Role s_role,
                            const std::vector<std::string>& specs,
                            bool reverse )
  : role( s_role ), edge( s_link ), conversations( s_role == LISTEN, reverse, specs.size() )
{
  if ( specs.size() > MAX_FORWARDS ) {
    throw NetworkException( "UDP forwarding: too many forwarding specifications", 0 );
  }

  try {
    for ( std::vector<std::string>::const_iterator i = specs.begin(); i != specs.end(); i++ ) {
      ForwardSpec spec( *i );
      Forward forward;
      forward.description = *i;

      if ( role == LISTEN ) {
        const bool wildcard = spec.bind_host.empty() || spec.bind_host == "*";
        const std::string where = spec.bind_host + ":" + spec.bind_port;
        Resolved ai( where,
                     wildcard ? NULL : spec.bind_host.c_str(),
                     spec.bind_port.c_str(),
                     AI_PASSIVE | AI_NUMERICSERV );

        forward.listen_fd = socket( ai.res->ai_family, SOCK_DGRAM, 0 );
        if ( forward.listen_fd < 0 ) {
          throw NetworkException( "UDP forwarding: socket", errno );
        }
        set_cloexec( forward.listen_fd );
        edge.forwards.push_back( forward ); /* now owned; closed by the destructor */

        if ( bind( forward.listen_fd, ai.res->ai_addr, ai.res->ai_addrlen ) < 0 ) {
          throw NetworkException( "UDP forwarding: bind " + where, errno );
        }
      } else {
        Resolved ai( spec.target_host + ":" + spec.target_port,
                     spec.target_host.c_str(),
                     spec.target_port.c_str(),
                     AI_NUMERICSERV );
        if ( static_cast<size_t>( ai.res->ai_addrlen ) > sizeof( forward.target ) ) {
          throw NetworkException( "UDP forwarding: unsupported address", 0 );
        }
        memcpy( &forward.target, ai.res->ai_addr, ai.res->ai_addrlen );
        forward.target_len = ai.res->ai_addrlen;
        edge.forwards.push_back( forward );
      }
    }
  } catch ( ... ) {
    for ( std::vector<Forward>::iterator i = edge.forwards.begin(); i != edge.forwards.end(); i++ ) {
      if ( i->listen_fd >= 0 ) {
        ::close( i->listen_fd );
      }
    }
    throw;
  }
}

UDPForwarder::Edge::Edge( SideChannelLink* s_link )
  : link( s_link ), verbose( 0 ), forwards(), sockets()
{}

UDPForwarder::~UDPForwarder()
{
  for ( std::vector<Forward>::iterator i = edge.forwards.begin(); i != edge.forwards.end(); i++ ) {
    if ( i->listen_fd >= 0 ) {
      ::close( i->listen_fd );
    }
  }
  for ( std::vector<Socket>::iterator i = edge.sockets.begin(); i != edge.sockets.end(); i++ ) {
    ::close( i->fd );
  }
}

std::vector<int> UDPForwarder::fds( void ) const
{
  std::vector<int> ret;
  for ( std::vector<Forward>::const_iterator i = edge.forwards.begin(); i != edge.forwards.end(); i++ ) {
    if ( i->listen_fd >= 0 ) {
      ret.push_back( i->listen_fd );
    }
  }
  for ( std::vector<Socket>::const_iterator i = edge.sockets.begin(); i != edge.sockets.end(); i++ ) {
    ret.push_back( i->fd );
  }
  return ret;
}

Gosh::DatagramLink UDPForwarder::link_now( void ) const
{
  Gosh::DatagramLink now;
  now.ready = edge.link->sidechannel_ready();
  /* (The fork asked for the size only of a session that was ready, and for the loss only when it had a
     datagram to fragment; here the loss is read each time. Both are plain values the session already holds.) */
  now.max_payload = now.ready ? edge.link->sidechannel_max_payload() : 0;
  now.loss = now.ready ? edge.link->sidechannel_loss() : 0;
  return now;
}

bool UDPForwarder::Edge::open( uint32_t handle, uint8_t forward, uint16_t id )
{
  const Forward& f = forwards[forward];
  Socket s;
  s.handle = handle;
  s.fd = socket( f.target.sa.sa_family, SOCK_DGRAM, 0 );
  if ( s.fd < 0 ) {
    return false;
  }
  set_cloexec( s.fd );
  if ( connect( s.fd, &f.target.sa, f.target_len ) < 0 ) {
    ::close( s.fd );
    return false;
  }
  if ( verbose ) {
    fprintf( stderr, "UDP forwarding: new flow %d for %s\n", static_cast<int>( id ), f.description.c_str() );
  }
  sockets.push_back( s );
  return true;
}

void UDPForwarder::Edge::close( uint32_t handle )
{
  for ( std::vector<Socket>::iterator i = sockets.begin(); i != sockets.end(); i++ ) {
    if ( i->handle == handle ) {
      ::close( i->fd );
      sockets.erase( i );
      return;
    }
  }
}

void UDPForwarder::Edge::to_target( uint32_t handle, const std::string& datagram )
{
  for ( std::vector<Socket>::iterator i = sockets.begin(); i != sockets.end(); i++ ) {
    if ( i->handle == handle ) {
      ::send( i->fd, datagram.data(), datagram.size(), MSG_DONTWAIT );
      return;
    }
  }
}

void UDPForwarder::Edge::to_application( uint8_t forward, const std::string& peer, const std::string& datagram )
{
  sendto( forwards[forward].listen_fd,
          datagram.data(),
          datagram.size(),
          MSG_DONTWAIT,
          reinterpret_cast<const struct sockaddr*>( peer.data() ),
          peer.size() );
}

void UDPForwarder::say_if_dropped( uint64_t dropped_before, size_t len )
{
  if ( edge.verbose && !dropped_before && conversations.get_dropped_oversize() ) {
    fprintf( stderr, "UDP forwarding: dropping %d-byte datagram (too many fragments)\n", static_cast<int>( len ) );
  }
}

void UDPForwarder::read_fd( int fd )
{
  const uint64_t now = timestamp();
  char buf[65536];

  if ( role == LISTEN ) {
    size_t forward = 0;
    while ( forward < edge.forwards.size() && edge.forwards[forward].listen_fd != fd ) {
      forward++;
    }
    if ( forward == edge.forwards.size() ) {
      return;
    }

    for ( int n = 0; n < MAX_READS_PER_WAKEUP; n++ ) {
      Addr from;
      socklen_t from_len = sizeof( from );
      memset( &from, 0, sizeof( from ) );
      ssize_t len = recvfrom( fd, buf, sizeof( buf ), MSG_DONTWAIT, &from.sa, &from_len );
      if ( len < 0 ) {
        return; /* drained, or a transient error: either way, nothing to forward */
      }

      const uint64_t dropped = conversations.get_dropped_oversize();
      conversations.from_application(
        forward, std::string( reinterpret_cast<const char*>( &from ), from_len ), buf, len, now, link_now(), edge );
      say_if_dropped( dropped, len );
    }
  } else {
    for ( int n = 0; n < MAX_READS_PER_WAKEUP; n++ ) {
      const Socket* s = NULL;
      for ( std::vector<Socket>::const_iterator i = edge.sockets.begin(); i != edge.sockets.end(); i++ ) {
        if ( i->fd == fd ) {
          s = &*i;
          break;
        }
      }
      if ( !s ) {
        return;
      }

      ssize_t len = recv( fd, buf, sizeof( buf ), MSG_DONTWAIT );
      if ( len < 0 ) {
        /* drained, or e.g. ECONNREFUSED relayed from an ICMP error: ignore */
        return;
      }

      const uint64_t dropped = conversations.get_dropped_oversize();
      conversations.from_target( s->handle, buf, len, now, link_now(), edge );
      say_if_dropped( dropped, len );
    }
  }
}

void UDPForwarder::sidechannel_recv( const std::string& payload )
{
  conversations.payload( payload, timestamp(), edge );
}
