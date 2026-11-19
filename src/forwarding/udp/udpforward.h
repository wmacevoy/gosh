/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef UDPFORWARD_HPP
#define UDPFORWARD_HPP

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "src/forwarding/udp/conversations.h"
#include "src/forwarding/forward.h"
#include "src/network/network.h"
#include "src/network/sidechannel.h"

/*
 * UDP port forwarding over the mosh side channel ("-LU" and "-RU", in
 * the manner of ssh's "-L" and "-R" for TCP).
 *
 * For a local forward (-LU), the client listens on a local UDP port.
 * Each datagram it receives is sent to the server over the side
 * channel, and the server re-sends it to a fixed target from a socket
 * of its own.  Whatever the target
 * replies travels back the same way and is delivered to the original
 * local sender.
 *
 * Delivery is exactly as unreliable as UDP itself: nothing is
 * retransmitted or reordered.  A datagram is never delivered more than
 * once, even if an attacker replays it.
 *
 * A datagram that does not fit in a single mosh datagram is sent as
 * several fragments and put back together at the other end (see
 * DatagramAssembly).  If a fragment is lost, so is the datagram, unless
 * the sender added parity fragments, from which the receiver can
 * rebuild one lost fragment in every group of eight.
 *
 * Whether parity is worth its cost depends on how lossy the path is and
 * how many fragments a datagram needs, so by default the sender decides
 * for each datagram: it adds parity when, at the loss rate the session
 * is seeing (SideChannelLink::sidechannel_loss), the datagram would
 * otherwise be lost more than PARITY_WORTHWHILE of the time.
 *
 * Both ends are given the same list of forwarding specifications, so
 * nothing but data needs to cross the network: a forwarded datagram
 * names its specification by index and its conversation by a flow id
 * that the listening end chooses.
 *
 * A remote forward (-RU) is the same with the ends exchanged: the
 * server listens and the client sends to the target.
 */

namespace Network {

/*
 * (gosh) Since queue item 16 this class is the edge of UDP forwards: it owns the listeners and
 * the sockets to targets, reads the clock, and decides nothing. Which conversation a datagram
 * belongs to, fragments, replay and expiry are Gosh::Conversations (conversations.h), which
 * its Edge is the Out of. Its public face is the fork's, so the fork's unit test still drives it.
 */
class UDPForwarder : public SideChannelSink, public Gosh::ConversationRules
{
public:
  enum Role
  {
    LISTEN, /* accept datagrams on ports of this machine */
    CONNECT /* relay them to the targets */
  };

  static constexpr size_t MAX_FORWARDS = 16;
  static constexpr int MAX_READS_PER_WAKEUP = 16;

private:
  struct Forward
  {
    int listen_fd; /* LISTEN only */
    Addr target;   /* CONNECT only */
    socklen_t target_len;
    std::string description;

    Forward() : listen_fd( -1 ), target(), target_len( 0 ), description() {}
  };

  /* CONNECT: our socket to a target, for the conversation the logic names by this handle. */
  struct Socket
  {
    uint32_t handle;
    int fd;
  };

  /*
   * The edge of the logic: the logic is handed this, never the UDPForwarder that holds the logic, so
   * it holds only what the logic's callbacks use: the side channel, the forwards, the sockets, verbose.
   */
  struct Edge
  {
    SideChannelLink* link;
    unsigned int verbose;
    std::vector<Forward> forwards;
    std::vector<Socket> sockets;

    Edge( SideChannelLink* s_link );

    /* What the logic asks of its edge (conversations.h). */
    void send( const std::string& payload ) { link->sidechannel_send( payload ); }
    bool open( uint32_t handle, uint8_t forward, uint16_t id );
    void close( uint32_t handle );
    void to_target( uint32_t handle, const std::string& datagram );
    void to_application( uint8_t forward, const std::string& peer, const std::string& datagram );
  };

  Role role;
  Edge edge;
  Gosh::Conversations<Edge> conversations;

  Gosh::DatagramLink link_now( void ) const;
  void say_if_dropped( uint64_t dropped_before, size_t len );

  /* unused */
  UDPForwarder( const UDPForwarder& );
  UDPForwarder& operator=( const UDPForwarder& );

public:
  /*
   * Throws NetworkException if a specification can't be parsed, resolved or bound.
   *
   * Normally the client listens and the server connects.  For forwards
   * the other way round (the server listens), both ends say "reverse",
   * which keeps the two directions' datagrams apart on the wire.
   */
  UDPForwarder( SideChannelLink* s_link,
                Role s_role,
                const std::vector<std::string>& specs,
                bool reverse = false );
  ~UDPForwarder();

  void set_verbose( unsigned int s_verbose ) { edge.verbose = s_verbose; }

  /* When to add parity to the datagrams we fragment.  (What we receive may have it regardless.) */
  void set_parity_mode( ParityMode s_mode ) { conversations.set_parity_mode( s_mode ); }

  /* Sockets to poll for reading; pass each readable one to read_fd(). */
  std::vector<int> fds( void ) const;
  void read_fd( int fd );

  /* A forwarded datagram has arrived from the other end of the session. */
  void sidechannel_recv( const std::string& payload );

  size_t flow_count( void ) const { return conversations.flow_count(); }
  uint64_t get_dropped_oversize( void ) const { return conversations.get_dropped_oversize(); }

  /* Datagrams we sent in fragments; and of those received so: rebuilt, rebuilt using parity, given up on. */
  uint64_t get_fragmented( void ) const { return conversations.get_fragmented(); }
  uint64_t get_with_parity( void ) const { return conversations.get_with_parity(); }
  uint64_t get_reassembled( void ) const { return conversations.get_reassembled(); }
  uint64_t get_recovered( void ) const { return conversations.get_recovered(); }
  uint64_t get_abandoned( void ) const { return conversations.get_abandoned(); }
  size_t partial_count( void ) const { return conversations.partial_count(); }
};

}

#endif
