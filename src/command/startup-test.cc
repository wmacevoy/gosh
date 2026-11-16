/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CMD-1, REQ8/CMD-3: the startup message read a line at a time (startup.h): where the server
 * is, its port and key, which server answered; and what is refused. Plain strings.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/command/options.h"
#include "src/command/startup.h"
#include "src/command/stock.h"

using namespace Gosh;
using namespace Gosh::Command;

static int failures = 0;

static void check( const char* what, bool ok, const std::string& got = "" )
{
  printf( "%-84s %s%s\n", what, ok ? "ok" : "FAIL ", ok ? "" : got.c_str() );
  if ( !ok ) {
    failures++;
  }
}

static const std::string KEY = "abcdefghijklmnopqrstuv"; /* 22 characters of base 64 */

/* The failure a line gives, prefixed "!", or "taken" / "passed on". */
static std::string line( Startup& st, const std::string& raw, bool pick_server = true, bool port_range = false )
{
  try {
    return startup_line( st, raw, pick_server, port_range, "gosh" ) ? "taken" : "passed on";
  } catch ( const Failure& f ) {
    return "!" + f.message;
  }
}

static std::string failed( void ( *step )( Startup& ), Startup st )
{
  try {
    step( st );
  } catch ( const Failure& f ) {
    return "!" + f.message;
  }
  return "";
}

static void address( Startup& st )
{
  startup_address( st, "gosh", "me@h" );
}
static void key( Startup& st )
{
  startup_key( st, "gosh" );
}

int main()
{
  Startup st;
  check( "chatter is passed on", line( st, "Welcome to Debian\r" ) == "passed on" && !st.done );
  check( "MOSH IP: the address", line( st, "MOSH IP 10.0.0.1\r" ) == "taken" && st.ip == "10.0.0.1" );
  check( "a second MOSH IP is refused", line( st, "MOSH IP 10.0.0.2" ) == "!gosh error: detected attempt to redefine MOSH IP." );
  check( "MOSH CONNECT: the port and key, and the end of the message",
         line( st, "MOSH CONNECT 60001 " + KEY + "\r\n" ) == "taken" && st.port == "60001" && st.key == KEY && st.done );
  check( "then the address is the one said, with nothing to tell", startup_address( st, "gosh", "me@h" ).empty() && st.ip == "10.0.0.1" );
  check( "and there is a key", failed( key, st ).empty() );

  Startup bad;
  check( "MOSH IP with more words", line( bad, "MOSH IP a b" ) == "!Bad MOSH IP string: MOSH IP a b" );
  check( "MOSH CONNECT: a port that is not a number",
         line( bad, "MOSH CONNECT 6000x " + KEY ) == "!Bad MOSH CONNECT string: MOSH CONNECT 6000x " + KEY );
  check( "MOSH CONNECT: a key of 21 characters",
         line( bad, "MOSH CONNECT 60001 " + KEY.substr( 1 ) ).compare( 0, 25, "!Bad MOSH CONNECT string:" ) == 0 );
  check( "MOSH CONNECT: a key with a character not of base 64",
         line( bad, "MOSH CONNECT 60001 " + KEY.substr( 1 ) + "-" ).compare( 0, 25, "!Bad MOSH CONNECT string:" ) == 0 );
  check( "MOSH CONNECT: a word more", line( bad, "MOSH CONNECT 60001 " + KEY + " x" ).compare( 0, 25, "!Bad MOSH CONNECT string:" ) == 0 );
  check( "MOSH SSH_CONNECTION: not five words after it",
         line( bad, "MOSH SSH_CONNECTION 1 2 3" ) == "!Bad MOSH SSH_CONNECTION string: MOSH SSH_CONNECTION 1 2 3" );
  check( "nothing taken from a refused line", bad.ip.empty() && bad.port.empty() && bad.key.empty() && !bad.done );

  /* ---- the address from $SSH_CONNECTION (--experimental-remote-ip=remote) */
  Startup remote;
  check( "MOSH SSH_CONNECTION: the server's end of it",
         line( remote, "MOSH SSH_CONNECTION 10.0.0.9 5000 10.0.0.1 22" ) == "taken" && remote.ssh_ip == "10.0.0.1" );
  const std::string note = startup_address( remote, "gosh", "me@h" );
  check( "with no MOSH IP that is the address, and the user is told",
         remote.ip == "10.0.0.1" && note == "gosh: Using remote IP address 10.0.0.1 from $SSH_CONNECTION for hostname me@h", note );
  check( "and without a MOSH CONNECT: no startup message",
         failed( key, remote ) == "!gosh: Did not find the server's startup message. (Is gosh installed there? https://wmacevoy.github.io/gosh/USING.html)" );
  check( "with neither address: refused",
         failed( address, Startup() ) == "!gosh: Did not find remote IP address (is SSH ProxyCommand disabled?)." );

  /* ---- a port range a server does not know */
  Startup range;
  check( "Bad UDP port is passed on, and noted when a range was asked for",
         line( range, "mosh-server: Bad UDP port range (60001:60010)", true, true ) == "passed on" && range.bad_port );
  check( "and then that is the reason", failed( key, range ) == "!gosh: Server does not support UDP port range option." );
  Startup single;
  line( single, "mosh-server: Bad UDP port range (60001:60010)", true, false );
  check( "not noted when no range was asked for", !single.bad_port );

  /* ---- which server answered (REQ8/CMD-3) */
  Startup stock;
  check( "the stock line, when the host was asked to choose: taken, and the server is stock",
         line( stock, std::string( STOCK_SERVER_LINE ) + "\r" ) == "taken" && stock.stock );
  Startup named;
  check( "the stock line when the user named the server: just chatter",
         line( named, STOCK_SERVER_LINE, false ) == "passed on" && !named.stock );

  if ( failures ) {
    printf( "FAIL REQ1/CMD-1, REQ8/CMD-3: %d checks of the startup message failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/CMD-1, REQ8/CMD-3: the startup message\n" );
  return EXIT_SUCCESS;
}
