/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* (gosh) The fork's src/fuzz/forward_fuzz.h (mosh-1.4.0-pf.7), shared by forwarding's fuzzers; only its guard is changed. */

#ifndef GOSH_FORWARDING_FUZZ_H
#define GOSH_FORWARDING_FUZZ_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Shared by the port-forwarding fuzzers.
 *
 * Their input is a short header and then a series of records, each a
 * kind byte, a length byte and that many bytes of body.  The kinds mean
 * different things to each fuzzer, but always include "these bytes, raw,
 * as if from the network" alongside better-formed input, so that the
 * fuzzer can both throw junk and get deep into the state machines.
 */

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace ForwardFuzz {

class Input
{
private:
  const uint8_t* data;
  size_t size;

public:
  Input( const uint8_t* s_data, size_t s_size ) : data( s_data ), size( s_size ) {}

  bool empty( void ) const { return size == 0; }

  uint8_t byte( void )
  {
    if ( size == 0 ) {
      return 0;
    }
    size--;
    return *data++;
  }

  std::string bytes( size_t n )
  {
    n = n < size ? n : size;
    const std::string ret( reinterpret_cast<const char*>( data ), n );
    data += n;
    size -= n;
    return ret;
  }

  /* The next record's kind (0-3) and body.  False when the input is used up. */
  bool record( unsigned int& kind, std::string& body )
  {
    if ( size < 2 ) {
      return false;
    }
    kind = byte() & 3;
    body = bytes( byte() );
    return true;
  }
};

/* What is wrong is a bug in what is being fuzzed, not in the input. */
inline void require( bool ok )
{
  if ( !ok ) {
    abort();
  }
}

inline uint8_t at( const std::string& s, size_t i )
{
  return i < s.size() ? static_cast<uint8_t>( s[i] ) : 0;
}

inline void set_nonblocking( int fd )
{
  const int flags = fcntl( fd, F_GETFL );
  fcntl( fd, F_SETFL, flags | O_NONBLOCK );
}

/* A socket on the loopback interface, bound to a port of the system's choosing. */
inline int loopback_socket( int type, uint16_t& port, struct sockaddr_in& addr )
{
  const int fd = socket( AF_INET, type, 0 );
  require( fd >= 0 );
  const int on = 1;
  setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof( on ) );
  memset( &addr, 0, sizeof( addr ) );
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  require( bind( fd, reinterpret_cast<struct sockaddr*>( &addr ), sizeof( addr ) ) == 0 );
  socklen_t len = sizeof( addr );
  require( getsockname( fd, reinterpret_cast<struct sockaddr*>( &addr ), &len ) == 0 );
  port = ntohs( addr.sin_port );
  return fd;
}

}

#endif
