/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef TCPFORWARD_HPP
#define TCPFORWARD_HPP

#include <climits>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/network/network.h"
#include "src/network/sidechannel.h"
#include "src/forwarding/tcp/tcpinudp.h"
#include "src/forwarding/forward.h"
#include "src/forwarding/frame.h"

/*
 * TCP port forwarding over the mosh side channel.
 *
 * A forwarded TCP connection is carried as TCP *segments*, each one a
 * side-channel datagram in TCP-in-UDP form (tcpinudp.h).  mosh does not
 * retransmit or reorder them: the two TCP endpoints do, so there is one
 * layer of reliability, not two stacked.
 *
 * Each end turns connections into segments and back with an
 * AdapterPort (tcpadapter.h): a small TCP of our own, bridged to
 * ordinary sockets, since the kernel will not give an unprivileged
 * program TCP segments, only byte streams.
 *
 * One end of a forward listens for applications to connect, and the
 * other connects to the forward's target.  For a local forward (-L) the
 * client listens; for a remote forward (-R) the server does.  The two
 * directions are separate TCPForwarders with the roles exchanged, told
 * apart on the wire by their frame type.
 *
 * As with UDP forwarding, both ends are given the same list of
 * specifications.  A segment names its specification by index and its
 * connection by the TCP port at the listening end, which travels in the
 * frame header in place of the UDP forwarder's flow id; the port at the
 * connecting end is implied.  So of a TCPInUDP's two port fields, only
 * the listening end's (source_port going to the connecting end,
 * dest_port coming from it) means anything between the adapter and
 * its SegmentSink.
 */

namespace Network {

/* Where the adapter (tcpadapter.h) sends segments bound for the other end. */
class SegmentSink
{
public:
  /* False if nothing can be sent now (so new connections should be refused). */
  virtual bool segment_ready( void ) const = 0;

  /*
   * False while the session has lost contact with the other end.  It
   * may be back in a moment or tomorrow; connections are kept, not
   * timed out, in the meantime.
   */
  virtual bool link_up( void ) const = 0;

  /* The largest segment, in TCP-in-UDP datagram form, that can be sent. */
  virtual size_t max_datagram( void ) const = 0;

  virtual void to_network( uint8_t forward, const TCPInUDP& segment ) = 0;

  virtual ~SegmentSink() {}
};

class AdapterPort;

class TCPForwarder : public SideChannelSink, public SegmentSink
{
public:
  enum Role
  {
    LISTEN, /* applications on this machine connect to us */
    CONNECT /* we connect to the targets */
  };

  static constexpr size_t MAX_FORWARDS = 16;

private:
  SideChannelLink* link;
  Role role;
  uint8_t frame_type;
  unsigned int verbose;

  std::unique_ptr<AdapterPort> adapter;
  size_t forwards; /* how many there are */

  uint64_t next_seq;
  Gosh::ReplayWindow replay;

  uint64_t dropped_oversize;

  /* unused */
  TCPForwarder( const TCPForwarder& );
  TCPForwarder& operator=( const TCPForwarder& );

public:
  /*
   * Throws NetworkException if a specification can't be parsed, resolved or bound.
   * The two ends of a set of forwards must be given the same frame type.
   */
  TCPForwarder( SideChannelLink* s_link,
                Role s_role,
                const std::vector<std::string>& specs,
                uint8_t s_frame_type = Gosh::TYPE_TCP_SEGMENT );
  ~TCPForwarder();

  void set_verbose( unsigned int s_verbose ) { verbose = s_verbose; }

  /*
   * A forward with no listening port and no address to connect to: a
   * stream between a socket the LISTEN end already has and one the
   * CONNECT end makes when asked.  It is forward number 0, and a
   * forwarder that has one was given no specifications.
   */
  void stream_spawner( const std::function<int()>& spawn ); /* CONNECT end */
  bool stream_open( int fd );                               /* LISTEN end; false if it can't be, now */
  void stream_allow( void ) { forwards = 1; }
  size_t connection_count( void ) const;

  std::vector<int> fds( void ) const;
  void read_fd( int fd );
  void tick( void );
  int wait_time( void ) const;

  /* SideChannelSink */
  void sidechannel_recv( const std::string& payload );

  /* SegmentSink */
  bool segment_ready( void ) const { return link->sidechannel_ready(); }
  bool link_up( void ) const { return link->sidechannel_connected(); }
  size_t max_datagram( void ) const;
  void to_network( uint8_t forward, const TCPInUDP& segment );

  uint64_t get_dropped_oversize( void ) const { return dropped_oversize; }
};

}

#endif
