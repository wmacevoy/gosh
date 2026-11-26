/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef PUNCH_HPP
#define PUNCH_HPP

#include <string>
#include <vector>

#include "src/meetup/ticket.h"
#include "src/network/network.h"
#include "src/pathfinding/got.h"
#include "src/pathfinding/imp/finding.h"
#include "src/stun/stun.h"

/*
 * A way straight through, for two machines that are each behind an address translator (a NAT)
 * and so cannot simply be sent to (REQ1/PATH-3); and, where the user has a meetup and a ticket for
 * it, a way through that (REQ15/PATH-4).
 *
 * A NAT lets a datagram in from outside only where one has lately gone out the other way. So if
 * both ends send to each other at once, each one's NAT takes the other's datagrams for answers to
 * its own, and lets them in: "hole punching" (RFC 5128). For that each end has to know where the
 * other appears to be, from outside. It finds out its own outside address by asking a server that
 * will say where a datagram came from (STUN, RFC 8489); it adds its addresses on its own network,
 * in case the other end is on that too; and the two tell each other over the ssh connection that
 * carries the relay (relay's process.h), whose two ends hold one of these each.
 *
 * It does not always work. Some NATs give a different outside address for each destination, and
 * then what the server saw is no use to anyone else. Nothing is lost by trying: the relay goes on
 * carrying the session over ssh until a way through is found, and again if it closes.
 *
 * This is the edge (design v2, 2.2, path-finding's "socket, interfaces, asker"): the socket, this
 * machine's addresses, the time of day, and the process that looks names up and asks the STUN
 * servers. What it decides is finding.h's, and the bytes of STUN are stun.h's. IPv4 and IPv6, on
 * one socket where the system allows (REQ17/PATH-8).
 */

namespace Gosh::Pathfinding {

class Punch
{
public:
  typedef Pathfinding::Got Got;
  static constexpr Got NOTHING = Pathfinding::NOTHING;
  static constexpr Got OTHER = Pathfinding::OTHER;
  static constexpr Got DATA = Pathfinding::DATA;
  static constexpr Got GOODBYE = Pathfinding::GOODBYE;

private:
  int sock;
  int family; /* (gosh) AF_INET6 if the socket takes both families; AF_INET if it could not */
  Imp::PathFinding finding;

  void flush( void );

  /* unused */
  Punch( const Punch& );
  Punch& operator=( const Punch& );

public:
  /*
   * key: as Crypto::Base64Key prints it. The end that made it up is the initiator. ask:
   * "host:port" of STUN servers, asked (from another process, so as not to wait on name lookups)
   * where we appear to be. port_low, port_high: the socket's port is the first free one in this
   * range, or any port if none is free or the range is 0 (issue #2: a server's firewall lets in
   * the ports sessions use, and a probe from a NAT that changes ports gets through only to one of
   * those). Throws NetworkException if there is no socket to be had.
   */
  Punch( bool s_initiator,
         const std::string& key,
         const std::vector<std::string>& ask,
         uint64_t now,
         int port_low = 0,
         int port_high = 0 );
  ~Punch();

  int fd( void ) const { return sock; }
  uint16_t local_port( void ) const;

  void add_candidate( const std::string& ip_port ) { finding.add_candidate( ip_port ); }
  /* Where we may be reached, as far as has been learnt since this was last asked. */
  std::vector<std::string> take_local_candidates( void ) { return finding.take_local_candidates(); }

  /* (gosh) Use a meetup as well: at this address ("ip:port"), with this ticket. False if the address is not one. */
  bool use_meetup( const std::string& ip_port, const Gosh::Meetup::Ticket& t );
  bool meetup_registered( void ) const { return finding.meetup_registered(); }

  void tick( uint64_t now );
  /* Reads one datagram, if there is one. */
  Got receive( uint64_t now, std::string& datagram );

  /* Whether there is, as far as we know, a way through just now. */
  bool open( uint64_t now ) const { return finding.open( now ); }
  uint64_t last_heard( void ) const { return finding.last_heard(); }
  /* Where the other end is; or which meetup, if datagrams are being passed on to it through one. */
  std::string where( void ) const { return finding.where(); }
  bool direct( void ) const { return finding.direct(); }

  void send( const std::string& datagram );
  void goodbye( void );
};

}

#endif
