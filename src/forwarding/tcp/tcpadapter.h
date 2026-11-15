/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef TCPADAPTER_HPP
#define TCPADAPTER_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "src/crypto/prng.h"
#include "src/forwarding/tcp/connection.h"
#include "src/forwarding/tcp/tcpforward.h"

/*
 * forwarding: listeners and targets (design v2, 2.2). The sockets of forwarded TCP connections.
 *
 *   application <-- kernel TCP --> socket | AdapterPort | Connections <== segments ==> other end
 *
 * THIS IS AN EDGE. It owns the listening sockets, each connection's socket, and the targets'
 * addresses; it reads the clock, and draws each connection's first sequence number. It decides
 * nothing about a connection: connection.h does, and tells this what to do with the sockets.
 *
 * On the listening end it accepts applications' connections and hands each to the logic. On the
 * connecting end, when the logic asks, it connects to the forward's target (or has a function
 * make the socket, for a stream).
 *
 * How it fails: a listener that cannot be bound throws from add_listener; anything that goes
 * wrong with a connection's socket is told to the logic, which ends that connection and tells
 * the other end. Nothing here ends the session.
 */
namespace Network {

class AdapterPort
{
private:
  typedef Gosh::Connections<AdapterPort> Logic;
  typedef Logic::Id Id;

public:
  /* The logic's numbers, for the code that names them through this class. They are the logic's: one place. */
  static constexpr size_t MAX_CONNECTIONS = Logic::MAX_CONNECTIONS;
  static constexpr size_t SEND_BUFFER = Logic::SEND_BUFFER;
  static constexpr size_t RECV_BUFFER = Logic::RECV_BUFFER;
  static constexpr uint32_t MIN_MSS = Logic::MIN_MSS;

private:
  friend class Gosh::Connections<AdapterPort>; /* it calls the seven functions below */

  struct Listener
  {
    uint8_t forward;
    int fd;
  };

  struct Target
  {
    Addr addr;
    socklen_t len;
    bool present;
    std::function<int()> spawn; /* if set: makes the socket itself, connected to whatever it is to reach */

    Target() : addr(), len( 0 ), present( false ), spawn() {}
  };

  SegmentSink* sink;
  std::vector<Listener> listeners;
  std::vector<Target> targets; /* by forward index */
  std::map<Id, int> sockets;   /* each connection's socket */
  PRNG prng;
  Logic logic;

  /* What the session can do just now, for the logic. */
  Gosh::Link link( void ) const;
  int socket_of( Id id ) const; /* -1 if it has none */
  void accept_from( const Listener& listener, uint64_t now );

  /* What the logic asks for (connection.h says what each must do). */
  void send( uint8_t forward, const TCPInUDP& segment );
  uint32_t fresh_sequence( void );
  bool connect( Id id, uint8_t forward );
  int connecting( Id id );
  long deliver( Id id, const char* data, size_t length );
  void shut_write( Id id );
  void close( Id id, bool reset );

  /* unused */
  AdapterPort( const AdapterPort& );
  AdapterPort& operator=( const AdapterPort& );

public:
  /* s_client: this end listens for applications; the other end connects to the targets. */
  AdapterPort( SegmentSink* s_sink, bool s_client );
  ~AdapterPort();

  /* Listening end: start listening. Returns the address, for the user. Throws NetworkException. */
  std::string add_listener( uint8_t forward, const Gosh::ForwardSpec& spec );

  /* Connecting end: where connections on this forward go. */
  void add_target( uint8_t forward, const Addr& addr, socklen_t len );
  /*
   * Connecting end: connections on this forward go to whatever this makes. It returns a
   * connected stream socket (one end of a pair, say), or -1.
   */
  void add_spawner( uint8_t forward, const std::function<int()>& spawn );
  /*
   * Listening end: a connection on this forward, for a socket we already have, as if a listener
   * had just accepted it. False (and the socket is closed) if it cannot be forwarded now.
   */
  bool open_with( uint8_t forward, int fd, uint64_t now );

  /* For the loop: the sockets worth reading now; one of them is readable; timers; a segment has arrived. */
  std::vector<int> fds( void ) const;
  void read_fd( int fd );
  void tick( uint64_t now );
  int wait_time( uint64_t now ) const;
  void from_network( uint8_t forward, const TCPInUDP& segment );

  size_t connection_count( void ) const { return logic.count(); }
};

}

#endif
