/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

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
#include "src/forwarding/udpforward.h"
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
  : link( s_link ), role( s_role ), data_type( reverse ? TYPE_UDP_REVERSE_DATA : TYPE_UDP_DATA ),
    fragment_type( reverse ? TYPE_UDP_REVERSE_FRAGMENT : TYPE_UDP_FRAGMENT ), verbose( 0 ),
    parity_mode( PARITY_AUTO ), forwards(), flows(), next_flow_id( 0 ), next_seq( 0 ), replay(), partials(),
    finished(), dropped_oversize( 0 ), fragmented( 0 ), with_parity( 0 ), reassembled( 0 ), recovered( 0 ),
    abandoned( 0 )
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
        forwards.push_back( forward ); /* now owned; closed by the destructor */

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
        forwards.push_back( forward );
      }
    }
  } catch ( ... ) {
    for ( std::vector<Forward>::iterator i = forwards.begin(); i != forwards.end(); i++ ) {
      if ( i->listen_fd >= 0 ) {
        close( i->listen_fd );
      }
    }
    throw;
  }
}

UDPForwarder::~UDPForwarder()
{
  for ( std::vector<Forward>::iterator i = forwards.begin(); i != forwards.end(); i++ ) {
    if ( i->listen_fd >= 0 ) {
      close( i->listen_fd );
    }
  }
  for ( std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
    if ( i->fd >= 0 ) {
      close( i->fd );
    }
  }
}

std::vector<int> UDPForwarder::fds( void ) const
{
  std::vector<int> ret;
  for ( std::vector<Forward>::const_iterator i = forwards.begin(); i != forwards.end(); i++ ) {
    if ( i->listen_fd >= 0 ) {
      ret.push_back( i->listen_fd );
    }
  }
  for ( std::vector<Flow>::const_iterator i = flows.begin(); i != flows.end(); i++ ) {
    if ( i->fd >= 0 ) {
      ret.push_back( i->fd );
    }
  }
  return ret;
}

void UDPForwarder::remove_flow( size_t index )
{
  if ( flows[index].fd >= 0 ) {
    close( flows[index].fd );
  }
  flows.erase( flows.begin() + index );
}

void UDPForwarder::expire_flows( uint64_t now )
{
  for ( size_t i = flows.size(); i-- > 0; ) {
    if ( now - flows[i].last_used > FLOW_IDLE_TIMEOUT ) {
      remove_flow( i );
    }
  }
}

/* The returned pointer is only good until the next change to the flow table. */
UDPForwarder::Flow* UDPForwarder::new_flow( uint8_t forward, uint16_t id, uint64_t now )
{
  expire_flows( now );

  if ( flows.size() >= MAX_FLOWS ) {
    /* make room by forgetting the conversation that has been quiet longest */
    size_t oldest = 0;
    for ( size_t i = 1; i < flows.size(); i++ ) {
      if ( flows[i].last_used < flows[oldest].last_used ) {
        oldest = i;
      }
    }
    remove_flow( oldest );
  }

  Flow flow;
  flow.forward = forward;
  flow.id = id;
  flow.last_used = now;

  if ( role == CONNECT ) {
    const Forward& f = forwards[forward];
    flow.fd = socket( f.target.sa.sa_family, SOCK_DGRAM, 0 );
    if ( flow.fd < 0 ) {
      return NULL;
    }
    set_cloexec( flow.fd );
    if ( connect( flow.fd, &f.target.sa, f.target_len ) < 0 ) {
      close( flow.fd );
      return NULL;
    }
    if ( verbose ) {
      fprintf( stderr, "UDP forwarding: new flow %d for %s\n", static_cast<int>( id ), f.description.c_str() );
    }
  }

  flows.push_back( flow );
  return &flows.back();
}

void UDPForwarder::to_network( Flow& flow, const char* data, size_t len )
{
  if ( !link->sidechannel_ready() ) {
    return; /* peer doesn't (yet) support forwarding, or we're not connected */
  }

  const size_t limit = link->sidechannel_max_payload();
  const std::string datagram( data, len );

  if ( FRAME_HEADER_LEN + len <= limit ) {
    link->sidechannel_send( encode_frame( data_type, flow.forward, flow.id, next_seq++, datagram ) );
    return;
  }

  /* Too big to go in one piece.  Is it worth protecting the pieces? */
  std::vector<std::string> fragments;
  bool parity = false;
  if ( limit > FRAME_HEADER_LEN ) {
    const unsigned int count = DatagramAssembly::piece_count( len, limit - FRAME_HEADER_LEN );
    parity = parity_mode == PARITY_ALWAYS
             || ( parity_mode == PARITY_AUTO && parity_worthwhile( link->sidechannel_loss(), count ) );
    fragments = DatagramAssembly::split( datagram, limit - FRAME_HEADER_LEN, parity );
  }
  if ( fragments.empty() ) {
    if ( verbose && !dropped_oversize ) {
      fprintf( stderr,
               "UDP forwarding: dropping %d-byte datagram (too many fragments)\n",
               static_cast<int>( len ) );
    }
    dropped_oversize++;
    return;
  }

  /* The receiver relies on these sequence numbers being consecutive. */
  for ( std::vector<std::string>::const_iterator i = fragments.begin(); i != fragments.end(); i++ ) {
    link->sidechannel_send( encode_frame( fragment_type, flow.forward, flow.id, next_seq++, *i ) );
  }
  fragmented++;
  with_parity += parity ? 1 : 0;
}

void UDPForwarder::read_fd( int fd )
{
  const uint64_t now = timestamp();
  char buf[65536];

  if ( role == LISTEN ) {
    size_t forward = 0;
    while ( forward < forwards.size() && forwards[forward].listen_fd != fd ) {
      forward++;
    }
    if ( forward == forwards.size() ) {
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

      /* which conversation is this? */
      Flow* flow = NULL;
      for ( std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
        if ( i->forward == forward && i->peer_len == from_len && memcmp( &i->peer, &from, from_len ) == 0 ) {
          flow = &*i;
          break;
        }
      }
      if ( !flow ) {
        /* choose an id that no live conversation on this forward is using */
        for ( std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); ) {
          if ( i->forward == forward && i->id == next_flow_id ) {
            next_flow_id++;
            i = flows.begin();
          } else {
            i++;
          }
        }
        flow = new_flow( forward, next_flow_id++, now );
        if ( !flow ) {
          continue;
        }
        flow->peer = from;
        flow->peer_len = from_len;
      }

      flow->last_used = now;
      to_network( *flow, buf, len );
    }
  } else {
    for ( int n = 0; n < MAX_READS_PER_WAKEUP; n++ ) {
      Flow* flow = NULL;
      for ( std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
        if ( i->fd == fd ) {
          flow = &*i;
          break;
        }
      }
      if ( !flow ) {
        return;
      }

      ssize_t len = recv( fd, buf, sizeof( buf ), MSG_DONTWAIT );
      if ( len < 0 ) {
        /* drained, or e.g. ECONNREFUSED relayed from an ICMP error: ignore */
        return;
      }

      flow->last_used = now;
      to_network( *flow, buf, len );
    }
  }
}

void UDPForwarder::sidechannel_recv( const std::string& payload )
{
  uint8_t type, forward;
  uint16_t flow_id;
  uint64_t seq;
  std::string data;

  if ( !decode_frame( payload, type, forward, flow_id, seq, data ) || forward >= forwards.size()
       || ( type != data_type && type != fragment_type ) ) {
    return;
  }

  /* The session authenticates every datagram but does not stop an old
     one from being replayed, so that is up to us. */
  if ( !replay.accept( seq ) ) {
    return;
  }

  const uint64_t now = timestamp();

  if ( type == fragment_type ) {
    fragment_recv( forward, flow_id, seq, data, now );
  } else {
    deliver( forward, flow_id, data, now );
  }
}

void UDPForwarder::expire_partials( uint64_t now )
{
  for ( std::map<uint64_t, Partial>::iterator i = partials.begin(); i != partials.end(); ) {
    if ( now - i->second.started > REASSEMBLY_TIMEOUT ) {
      partials.erase( i++ ); /* a fragment was lost, and with it the datagram */
      abandoned++;
    } else {
      i++;
    }
  }
}

void UDPForwarder::fragment_recv( uint8_t forward,
                                  uint16_t flow_id,
                                  uint64_t seq,
                                  const std::string& fragment,
                                  uint64_t now )
{
  unsigned int index;
  if ( !DatagramAssembly::index_of( fragment, index ) ) {
    return;
  }
  const uint64_t first_seq = seq - index;

  if ( std::find( finished.begin(), finished.end(), first_seq ) != finished.end() ) {
    return; /* already rebuilt without this one (typically, parity that wasn't needed) */
  }

  expire_partials( now );

  std::map<uint64_t, Partial>::iterator i = partials.find( first_seq );
  if ( i == partials.end() ) {
    if ( partials.size() >= MAX_PARTIALS ) {
      partials.erase( partials.begin() ); /* the oldest */
      abandoned++;
    }
    Partial fresh;
    fresh.forward = forward;
    fresh.flow_id = flow_id;
    fresh.started = now;
    i = partials.insert( std::make_pair( first_seq, fresh ) ).first;
  } else if ( i->second.forward != forward || i->second.flow_id != flow_id ) {
    return;
  }

  std::string datagram;
  bool used_parity = false;
  switch ( i->second.assembly.add( fragment, datagram, used_parity ) ) {
    case DatagramAssembly::COMPLETE:
      partials.erase( i );
      finished.push_back( first_seq );
      if ( finished.size() > MAX_REMEMBERED ) {
        finished.pop_front();
      }
      reassembled++;
      recovered += used_parity ? 1 : 0;
      deliver( forward, flow_id, datagram, now );
      break;
    case DatagramAssembly::REJECTED:
      if ( i->second.assembly.empty() ) {
        partials.erase( i ); /* don't let junk hold a place */
      }
      break;
    case DatagramAssembly::INCOMPLETE:
      break;
  }
}

/* A whole datagram from the other end: pass it on. */
void UDPForwarder::deliver( uint8_t forward, uint16_t flow_id, const std::string& datagram, uint64_t now )
{
  Flow* flow = NULL;
  for ( std::vector<Flow>::iterator i = flows.begin(); i != flows.end(); i++ ) {
    if ( i->forward == forward && i->id == flow_id ) {
      flow = &*i;
      break;
    }
  }

  if ( role == LISTEN ) {
    if ( !flow ) {
      return; /* a reply to a conversation we have forgotten */
    }
    flow->last_used = now;
    sendto( forwards[forward].listen_fd,
            datagram.data(),
            datagram.size(),
            MSG_DONTWAIT,
            &flow->peer.sa,
            flow->peer_len );
  } else {
    if ( !flow ) {
      flow = new_flow( forward, flow_id, now );
      if ( !flow ) {
        return;
      }
    }
    flow->last_used = now;
    send( flow->fd, datagram.data(), datagram.size(), MSG_DONTWAIT );
  }
}
