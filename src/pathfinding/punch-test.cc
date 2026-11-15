/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-3, REQ15/PATH-4, REQ15/PATH-5: path-finding's two ends (punch.h), with real sockets
 * on loopback and the real cipher: a way straight through when each knows where the other is;
 * a way through a meetup when neither does, with a ticket; and a probe of the wrong length not
 * opened (the fifth security fault: the cipher guards its buffers with an assert alone).
 *
 * The meetup is in this process: a loopback socket served by the meetup's own logic
 * (registrations.h, answer.h), so that both halves are the code that runs. Times are this
 * process's monotonic clock; each case waits at most a few seconds.
 */

#include <cstdio>
#include <ctime>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/meetup/answer.h"
#include "src/meetup/registrations.h"
#include "src/pathfinding/punch.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static uint64_t now_ms( void )
{
  struct timespec t;
  clock_gettime( CLOCK_MONOTONIC, &t );
  return static_cast<uint64_t>( t.tv_sec ) * 1000 + t.tv_nsec / 1000000;
}

namespace {
const std::string MASTER( "0123456789abcdef" );

std::string ticket_key_of( const std::string& id )
{
  return Gosh::ticket_key( MASTER, id );
}

/* A meetup, on a loopback socket of its own (IPv4 or IPv6), served by the meetup's logic. */
struct Meetup
{
  int family;
  int sock;
  Gosh::Registrations registrations;
  int passed;

  explicit Meetup( int s_family = AF_INET )
    : family( s_family ), sock( socket( s_family, SOCK_DGRAM, 0 ) ), registrations(), passed( 0 )
  {
    Network::Addr a;
    memset( &a, 0, sizeof( a ) );
    if ( family == AF_INET6 ) {
      a.sin6.sin6_family = AF_INET6;
      a.sin6.sin6_addr = in6addr_loopback;
      bind( sock, &a.sa, sizeof( a.sin6 ) );
    } else {
      a.sin.sin_family = AF_INET;
      a.sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
      bind( sock, &a.sa, sizeof( a.sin ) );
    }
  }
  ~Meetup() { close( sock ); }
  std::string where( void ) const
  {
    Network::Addr a;
    socklen_t len = sizeof( a );
    getsockname( sock, &a.sa, &len );
    return Network::format_ip_port( a );
  }
  void serve( void )
  {
    char buf[4096];
    Network::Addr from;
    socklen_t len = sizeof( from );
    const ssize_t n = recvfrom( sock, buf, sizeof( buf ), MSG_DONTWAIT, &from.sa, &len );
    if ( n < 0 ) {
      return;
    }
    const std::string w = Gosh::meetup_where_of( from );
    if ( Gosh::is_stun_request( buf, n ) ) {
      const std::string a = Gosh::stun_answer_to( buf, w );
      sendto( sock, a.data(), a.size(), 0, &from.sa, len );
      return;
    }
    const Gosh::Registrations::Send s = registrations.datagram( buf, n, w, time( NULL ), ticket_key_of );
    Network::Addr to;
    socklen_t to_len;
    if ( s.any && Gosh::meetup_addr_of( s.to, to, to_len ) ) {
      sendto( sock, s.bytes.data(), s.bytes.size(), 0, &to.sa, to_len );
      passed += s.bytes.size() != Gosh::MEETUP_ADDRESS_MESSAGE;
    }
  }
};

/* Runs both ends (and the meetup, if there is one) until done says so, or for at most ms. */
template<class Done>
bool run( Network::Punch& a, Network::Punch& b, Meetup* m, uint64_t ms, Done done, std::string* got_b = NULL )
{
  const uint64_t until = now_ms() + ms;
  while ( now_ms() < until ) {
    struct pollfd fds[3] = { { a.fd(), POLLIN, 0 }, { b.fd(), POLLIN, 0 }, { m ? m->sock : -1, POLLIN, 0 } };
    poll( fds, 3, 20 );
    const uint64_t now = now_ms();
    if ( m ) {
      m->serve();
    }
    std::string d;
    while ( a.receive( now, d ) != Network::Punch::NOTHING ) {
    }
    Network::Punch::Got got;
    while ( ( got = b.receive( now, d ) ) != Network::Punch::NOTHING ) {
      if ( got == Network::Punch::DATA && got_b ) {
        *got_b = d;
      }
    }
    a.tick( now );
    b.tick( now );
    if ( done( now ) ) {
      return true;
    }
  }
  return false;
}
}

int main()
{
  using Network::Punch;
  const std::string key = Crypto::Base64Key().printable_key();
  const std::vector<std::string> nobody;

  {
    Punch a( true, key, nobody, now_ms() ), b( false, key, nobody, now_ms() );
    a.add_candidate( "127.0.0.1:" + std::to_string( b.local_port() ) );
    b.add_candidate( "127.0.0.1:" + std::to_string( a.local_port() ) );
    const bool open = run( a, b, NULL, 3000, [&]( uint64_t now ) { return a.open( now ) && b.open( now ); } );
    check( "each told where the other is: a way straight through, both ways (REQ1/PATH-3)", open && a.direct() && b.direct() );
    std::string got;
    a.send( "a datagram of the session's" );
    run( a, b, NULL, 1000, [&]( uint64_t ) { return !got.empty(); }, &got );
    check( "and a datagram sent goes straight to the other end", got == "a datagram of the session's" );
  }

  {
    Meetup m;
    const Gosh::Ticket t = Gosh::make_ticket( MASTER, "random!!", time( NULL ) + 3600 );
    Punch a( true, key, nobody, now_ms() ), b( false, key, nobody, now_ms() );
    check( "a meetup is taken at an address, with a ticket", a.use_meetup( m.where(), t ) && b.use_meetup( m.where(), t ) );
    check( "and not at what is not an address", !a.use_meetup( "meetup.example:1", t ) );
    a.use_meetup( m.where(), t );
    const bool registered = run( a, b, &m, 3000, [&]( uint64_t ) { return a.meetup_registered() && b.meetup_registered(); } );
    check( "neither knowing where the other is, both register with the meetup (REQ15/PATH-5)", registered );
    const bool open = run( a, b, &m, 3000, [&]( uint64_t now ) { return a.open( now ) && b.open( now ); } );
    check( "and find each other through it: a way through, not straight (REQ15/PATH-4)",
           open && !a.direct() && !b.direct() && a.where().find( "meetup" ) != std::string::npos );
    std::string got;
    const int before = m.passed;
    a.send( "through the meetup" );
    run( a, b, &m, 1000, [&]( uint64_t ) { return got == "through the meetup"; }, &got );
    check( "a datagram sent goes through the meetup to the other end", got == "through the meetup" && m.passed > before );
  }

  {
    Meetup m;
    const Gosh::Ticket forged = Gosh::make_ticket( "not the master!!", "random!!", time( NULL ) + 3600 );
    Punch a( true, key, nobody, now_ms() ), b( false, key, nobody, now_ms() );
    a.use_meetup( m.where(), forged );
    b.use_meetup( m.where(), forged );
    const bool registered = run( a, b, &m, 1500, [&]( uint64_t ) { return a.meetup_registered() || b.meetup_registered(); } );
    check( "with a ticket the meetup did not make, neither is registered", !registered );
  }

  {
    Punch a( true, key, nobody, now_ms() );
    const int s = socket( AF_INET, SOCK_DGRAM, 0 );
    struct sockaddr_in to;
    memset( &to, 0, sizeof( to ) );
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    to.sin_port = htons( a.local_port() );
    std::string huge( Network::PUNCH_MARK, sizeof( Network::PUNCH_MARK ) );
    huge += std::string( 4000, 'x' );
    sendto( s, huge.data(), huge.size(), 0, reinterpret_cast<struct sockaddr*>( &to ), sizeof( to ) );
    usleep( 50000 );
    std::string d;
    const Punch::Got got = a.receive( now_ms(), d );
    check( "a probe-marked datagram of 4,008 bytes is not opened, and nothing is undone by it", got == Punch::OTHER );
    close( s );
  }

  /* REQ16/PATH-8: by IPv6, over ::1, if this machine has it. */
  const int probe6 = socket( AF_INET6, SOCK_DGRAM, 0 );
  Network::Addr loop6;
  memset( &loop6, 0, sizeof( loop6 ) );
  loop6.sin6.sin6_family = AF_INET6;
  loop6.sin6.sin6_addr = in6addr_loopback;
  const bool have6 = probe6 >= 0 && bind( probe6, &loop6.sa, sizeof( loop6.sin6 ) ) == 0;
  if ( probe6 >= 0 ) {
    close( probe6 );
  }
  if ( !have6 ) {
    printf( "SKIPPED: by IPv6: this machine has no ::1\n" );
  } else {
    {
      Punch a( true, key, nobody, now_ms() ), b( false, key, nobody, now_ms() );
      a.add_candidate( "[::1]:" + std::to_string( b.local_port() ) );
      b.add_candidate( "[::1]:" + std::to_string( a.local_port() ) );
      const bool open = run( a, b, NULL, 3000, [&]( uint64_t now ) { return a.open( now ) && b.open( now ); } );
      check( "over ::1, a way straight through by IPv6 (REQ16/PATH-8)", open && a.direct() && a.where().compare( 0, 5, "[::1]" ) == 0 );
      std::string got;
      a.send( "by IPv6" );
      run( a, b, NULL, 1000, [&]( uint64_t ) { return !got.empty(); }, &got );
      check( "and a datagram goes straight by it", got == "by IPv6" );
    }
    {
      Meetup m( AF_INET6 );
      const Gosh::Ticket t = Gosh::make_ticket( MASTER, "random!!", time( NULL ) + 3600 );
      Punch a( true, key, nobody, now_ms() ), b( false, key, nobody, now_ms() );
      a.use_meetup( m.where(), t );
      b.use_meetup( m.where(), t );
      const bool open = run( a, b, &m, 4000, [&]( uint64_t now ) { return a.open( now ) && b.open( now ); } );
      check( "a meetup at ::1 alone: both register, and find each other through it",
             open && a.meetup_registered() && b.meetup_registered() && !a.direct() );
      std::string got;
      a.send( "through an IPv6 meetup" );
      run( a, b, &m, 1000, [&]( uint64_t ) { return got == "through an IPv6 meetup"; }, &got );
      check( "and a datagram goes through it", got == "through an IPv6 meetup" );
    }
  }

  if ( failures ) {
    printf( "FAIL REQ1/PATH-3, REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: %d checks of path-finding failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/PATH-3, REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: path-finding, straight and through a meetup\n" );
  return 0;
}
