/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef FORWARDING_HPP
#define FORWARDING_HPP

#include <algorithm>
#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/forwarding/tcp/tcpforward.h"
#include "src/forwarding/udp/udpforward.h"

/* Everything a frontend forwards over the side channel, behind one event-loop interface. */

namespace Network {

class Forwarding : public SideChannelSink
{
private:
  std::unique_ptr<UDPForwarder> udp;
  std::unique_ptr<UDPForwarder> udp_remote; /* forwards the server listens for (-RU) */
  std::unique_ptr<TCPForwarder> tcp;
  std::unique_ptr<TCPForwarder> tcp_remote; /* forwards the server listens for (-R) */
  std::unique_ptr<TCPForwarder> stream;     /* a stream between a process at each end */

public:
  /* Throws NetworkException if a specification can't be parsed, resolved or bound. */
  Forwarding( SideChannelLink* link,
              bool client,
              const std::vector<std::string>& udp_specs,
              const std::vector<std::string>& tcp_specs,
              const std::vector<std::string>& tcp_remote_specs = std::vector<std::string>(),
              const std::vector<std::string>& udp_remote_specs = std::vector<std::string>(),
              bool with_stream = false )
    : udp(), udp_remote(), tcp(), tcp_remote(), stream()
  {
    if ( with_stream ) {
      stream.reset( new TCPForwarder( link,
                                      client ? TCPForwarder::LISTEN : TCPForwarder::CONNECT,
                                      std::vector<std::string>(),
                                      Gosh::TYPE_STREAM ) );
      stream->stream_allow();
    }
    if ( !udp_specs.empty() ) {
      udp.reset( new UDPForwarder( link, client ? UDPForwarder::LISTEN : UDPForwarder::CONNECT, udp_specs ) );
    }
    if ( !udp_remote_specs.empty() ) {
      /* the same again with the ends exchanged */
      udp_remote.reset( new UDPForwarder(
        link, client ? UDPForwarder::CONNECT : UDPForwarder::LISTEN, udp_remote_specs, true ) );
    }
    if ( !tcp_specs.empty() ) {
      tcp.reset( new TCPForwarder( link,
                                   client ? TCPForwarder::LISTEN : TCPForwarder::CONNECT,
                                   tcp_specs ) );
    }
    if ( !tcp_remote_specs.empty() ) {
      /* the same again with the ends exchanged */
      tcp_remote.reset( new TCPForwarder( link,
                                          client ? TCPForwarder::CONNECT : TCPForwarder::LISTEN,
                                          tcp_remote_specs,
                                          Gosh::TYPE_TCP_REVERSE ) );
    }
  }

  /* Server: what makes the socket for the stream when the client opens it. */
  void set_stream_spawner( const std::function<int()>& spawn )
  {
    if ( stream ) {
      stream->stream_spawner( spawn );
    }
  }
  /* Client: open the stream, for this socket.  False if that can't be done yet. */
  bool open_stream( int fd ) { return stream && stream->stream_open( fd ); }
  size_t stream_connections( void ) const { return stream ? stream->connection_count() : 0; }

  /* Give this to Transport::set_sidechannel_sink(). */
  SideChannelSink* sink( void ) { return this; }

  /*
   * (gosh) A payload from the other end: the byte after the mark says what kind it is, and so
   * whose. This was the fork's SideChannelMux, a table filled in above; there are seven kinds
   * and they are fixed, so it is a switch, and the seam's patch is the shorter for it.
   */
  void sidechannel_recv( const std::string& payload )
  {
    if ( payload.size() < 2 ) {
      return;
    }
    SideChannelSink* whose = NULL;
    switch ( static_cast<uint8_t>( payload[1] ) ) {
      case Gosh::TYPE_UDP_DATA:
      case Gosh::TYPE_UDP_FRAGMENT:
        whose = udp.get();
        break;
      case Gosh::TYPE_UDP_REVERSE_DATA:
      case Gosh::TYPE_UDP_REVERSE_FRAGMENT:
        whose = udp_remote.get();
        break;
      case Gosh::TYPE_TCP_SEGMENT:
        whose = tcp.get();
        break;
      case Gosh::TYPE_TCP_REVERSE:
        whose = tcp_remote.get();
        break;
      case Gosh::TYPE_STREAM:
        whose = stream.get();
        break;
    }
    if ( whose ) {
      whose->sidechannel_recv( payload );
    }
  }

  /*
   * When to add parity to the UDP datagrams this end has to fragment:
   * "auto" (the default: when the path is lossy enough), "always" or "never".
   * Returns false if the word isn't one of those.
   */
  bool set_fragment_parity( const std::string& mode )
  {
    UDPForwarder::ParityMode m;
    if ( mode == "auto" ) {
      m = UDPForwarder::PARITY_AUTO;
    } else if ( mode == "always" ) {
      m = UDPForwarder::PARITY_ALWAYS;
    } else if ( mode == "never" ) {
      m = UDPForwarder::PARITY_NEVER;
    } else {
      return false;
    }
    if ( udp ) {
      udp->set_parity_mode( m );
    }
    if ( udp_remote ) {
      udp_remote->set_parity_mode( m );
    }
    return true;
  }

  void set_verbose( unsigned int verbose )
  {
    if ( udp ) {
      udp->set_verbose( verbose );
    }
    if ( udp_remote ) {
      udp_remote->set_verbose( verbose );
    }
    if ( tcp ) {
      tcp->set_verbose( verbose );
    }
    if ( tcp_remote ) {
      tcp_remote->set_verbose( verbose );
    }
    if ( stream ) {
      stream->set_verbose( verbose );
    }
  }

  std::vector<int> fds( void ) const
  {
    std::vector<int> ret;
    if ( udp ) {
      ret = udp->fds();
    }
    if ( udp_remote ) {
      const std::vector<int> more = udp_remote->fds();
      ret.insert( ret.end(), more.begin(), more.end() );
    }
    if ( tcp ) {
      const std::vector<int> more = tcp->fds();
      ret.insert( ret.end(), more.begin(), more.end() );
    }
    if ( tcp_remote ) {
      const std::vector<int> more = tcp_remote->fds();
      ret.insert( ret.end(), more.begin(), more.end() );
    }
    if ( stream ) {
      const std::vector<int> more = stream->fds();
      ret.insert( ret.end(), more.begin(), more.end() );
    }
    return ret;
  }

  void read_fd( int fd )
  {
    /* each forwarder ignores descriptors that aren't its own */
    if ( udp ) {
      udp->read_fd( fd );
    }
    if ( udp_remote ) {
      udp_remote->read_fd( fd );
    }
    if ( tcp ) {
      tcp->read_fd( fd );
    }
    if ( tcp_remote ) {
      tcp_remote->read_fd( fd );
    }
    if ( stream ) {
      stream->read_fd( fd );
    }
  }

  /* Call once per trip around the event loop. */
  void tick( void )
  {
    if ( tcp ) {
      tcp->tick();
    }
    if ( tcp_remote ) {
      tcp_remote->tick();
    }
    if ( stream ) {
      stream->tick();
    }
  }

  /* Milliseconds until tick() next has something to do; INT_MAX if nothing is pending. */
  int wait_time( void ) const
  {
    return std::min( std::min( tcp ? tcp->wait_time() : INT_MAX, tcp_remote ? tcp_remote->wait_time() : INT_MAX ),
                     stream ? stream->wait_time() : INT_MAX );
  }
};

}

#endif
