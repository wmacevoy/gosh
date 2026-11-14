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

#ifndef UDPFORWARD_HPP
#define UDPFORWARD_HPP

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "src/forwarding/forward.h"
#include "src/forwarding/fragments.h"
#include "src/forwarding/frame.h"
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

class UDPForwarder : public SideChannelSink
{
public:
  enum Role
  {
    LISTEN, /* accept datagrams on ports of this machine */
    CONNECT /* relay them to the targets */
  };

  enum ParityMode
  {
    PARITY_AUTO,  /* when the measured loss makes it worthwhile */
    PARITY_ALWAYS,
    PARITY_NEVER
  };

  /* Datagrams being put back together: how many at once, and for how long. */
  static constexpr size_t MAX_PARTIALS = 64;
  static constexpr uint64_t REASSEMBLY_TIMEOUT = 1000; /* ms */
  static constexpr size_t MAX_REMEMBERED = 256;

  static constexpr size_t MAX_FORWARDS = 16;
  static constexpr size_t MAX_FLOWS = 64;
  static constexpr uint64_t FLOW_IDLE_TIMEOUT = 120000; /* ms */
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

  struct Flow
  {
    uint8_t forward;
    uint16_t id;
    int fd;    /* CONNECT: our socket, connected to the target */
    Addr peer; /* LISTEN: the local application that started this flow */
    socklen_t peer_len;
    uint64_t last_used;

    Flow() : forward( 0 ), id( 0 ), fd( -1 ), peer(), peer_len( 0 ), last_used( 0 ) {}
  };

  struct Partial
  {
    Gosh::DatagramAssembly assembly;
    uint8_t forward;
    uint16_t flow_id;
    uint64_t started;

    Partial() : assembly(), forward( 0 ), flow_id( 0 ), started( 0 ) {}
  };

  SideChannelLink* link;
  Role role;
  uint8_t data_type, fragment_type;
  unsigned int verbose;
  ParityMode parity_mode;

  std::vector<Forward> forwards;
  std::vector<Flow> flows;
  uint16_t next_flow_id;

  uint64_t next_seq;
  Gosh::ReplayWindow replay;

  /*
   * Fragments of a datagram are sent with consecutive sequence numbers,
   * so the first one's number (a fragment's own, less its index) names
   * the datagram.
   */
  std::map<uint64_t, Partial> partials;
  std::deque<uint64_t> finished; /* recently rebuilt, so stragglers don't start them again */

  uint64_t dropped_oversize;
  uint64_t fragmented, with_parity, reassembled, recovered, abandoned;

  void expire_partials( uint64_t now );
  void fragment_recv( uint8_t forward, uint16_t flow_id, uint64_t seq, const std::string& fragment, uint64_t now );
  void deliver( uint8_t forward, uint16_t flow_id, const std::string& datagram, uint64_t now );

  void expire_flows( uint64_t now );
  void remove_flow( size_t index );
  Flow* new_flow( uint8_t forward, uint16_t id, uint64_t now );
  void to_network( Flow& flow, const char* data, size_t len );

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

  void set_verbose( unsigned int s_verbose ) { verbose = s_verbose; }

  /* When to add parity to the datagrams we fragment.  (What we receive may have it regardless.) */
  void set_parity_mode( ParityMode s_mode ) { parity_mode = s_mode; }

  /* Sockets to poll for reading; pass each readable one to read_fd(). */
  std::vector<int> fds( void ) const;
  void read_fd( int fd );

  /* A forwarded datagram has arrived from the other end of the session. */
  void sidechannel_recv( const std::string& payload );

  size_t flow_count( void ) const { return flows.size(); }
  uint64_t get_dropped_oversize( void ) const { return dropped_oversize; }

  /* Datagrams we sent in fragments; and of those received so: rebuilt, rebuilt using parity, given up on. */
  uint64_t get_fragmented( void ) const { return fragmented; }
  uint64_t get_with_parity( void ) const { return with_parity; }
  uint64_t get_reassembled( void ) const { return reassembled; }
  uint64_t get_recovered( void ) const { return recovered; }
  uint64_t get_abandoned( void ) const { return abandoned; }
  size_t partial_count( void ) const { return partials.size(); }
};

}

#endif
