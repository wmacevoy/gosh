/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CMD-1, REQ8/CMD-3: the startup message read a line at a time (startup.h): where the server
 * is, its port and key, which server answered; and what is refused. Plain strings.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "src/command/imp/options.h"
#include "src/command/imp/startup.h"
#include "src/command/imp/stock.h"
#include "vendor/facts/facts.h"

using namespace Gosh;
using namespace Gosh::Command;
using namespace Gosh::Command::Imp;



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
/* REQ1/CMD-1, REQ8/CMD-3: the startup message */
FACTS( Startup )
{
  Startup st;
  FACT_CHECK( ( line( st, "Welcome to Debian\r" ) == "passed on" && !st.done ), "chatter is passed on" );
  FACT_CHECK( ( line( st, "MOSH IP 10.0.0.1\r" ) == "taken" && st.ip == "10.0.0.1" ), "MOSH IP: the address" );
  FACT_CHECK( line( st, "MOSH IP 10.0.0.2" ) == "!gosh error: detected attempt to redefine MOSH IP.", "a second MOSH IP is refused" );
  FACT_CHECK( ( line( st, "MOSH CONNECT 60001 " + KEY + "\r\n" ) == "taken" && st.port == "60001" && st.key == KEY && st.done ), "MOSH CONNECT: the port and key, and the end of the message" );
  FACT_CHECK( ( startup_address( st, "gosh", "me@h" ).empty() && st.ip == "10.0.0.1" ), "then the address is the one said, with nothing to tell" );
  FACT_CHECK( failed( key, st ).empty(), "and there is a key" );

  Startup bad;
  FACT_CHECK( line( bad, "MOSH IP a b" ) == "!Bad MOSH IP string: MOSH IP a b", "MOSH IP with more words" );
  FACT_CHECK( line( bad, "MOSH CONNECT 6000x " + KEY ) == "!Bad MOSH CONNECT string: MOSH CONNECT 6000x " + KEY, "MOSH CONNECT: a port that is not a number" );
  FACT_CHECK( line( bad, "MOSH CONNECT 60001 " + KEY.substr( 1 ) ).compare( 0, 25, "!Bad MOSH CONNECT string:" ) == 0, "MOSH CONNECT: a key of 21 characters" );
  FACT_CHECK( line( bad, "MOSH CONNECT 60001 " + KEY.substr( 1 ) + "-" ).compare( 0, 25, "!Bad MOSH CONNECT string:" ) == 0, "MOSH CONNECT: a key with a character not of base 64" );
  FACT_CHECK( line( bad, "MOSH CONNECT 60001 " + KEY + " x" ).compare( 0, 25, "!Bad MOSH CONNECT string:" ) == 0, "MOSH CONNECT: a word more" );
  FACT_CHECK( line( bad, "MOSH SSH_CONNECTION 1 2 3" ) == "!Bad MOSH SSH_CONNECTION string: MOSH SSH_CONNECTION 1 2 3", "MOSH SSH_CONNECTION: not five words after it" );
  FACT_CHECK( ( bad.ip.empty() && bad.port.empty() && bad.key.empty() && !bad.done ), "nothing taken from a refused line" );

  /* ---- the address from $SSH_CONNECTION (--experimental-remote-ip=remote) */
  Startup remote;
  FACT_CHECK( ( line( remote, "MOSH SSH_CONNECTION 10.0.0.9 5000 10.0.0.1 22" ) == "taken" && remote.ssh_ip == "10.0.0.1" ), "MOSH SSH_CONNECTION: the server's end of it" );
  const std::string note = startup_address( remote, "gosh", "me@h" );
  FACT_CHECK( ( remote.ip == "10.0.0.1" && note == "gosh: Using remote IP address 10.0.0.1 from $SSH_CONNECTION for hostname me@h" ), "with no MOSH IP that is the address, and the user is told" << ": " << ( note ) );
  FACT_CHECK( failed( key, remote ) == "!gosh: Did not find the server's startup message. (Is gosh installed there? https://wmacevoy.github.io/gosh/USING.html)", "and without a MOSH CONNECT: no startup message" );
  FACT_CHECK( failed( address, Startup() ) == "!gosh: Did not find remote IP address (is SSH ProxyCommand disabled?).", "with neither address: refused" );

  /* ---- a port range a server does not know */
  Startup range;
  FACT_CHECK( ( line( range, "mosh-server: Bad UDP port range (60001:60010)", true, true ) == "passed on" && range.bad_port ), "Bad UDP port is passed on, and noted when a range was asked for" );
  FACT_CHECK( failed( key, range ) == "!gosh: Server does not support UDP port range option.", "and then that is the reason" );
  Startup single;
  line( single, "mosh-server: Bad UDP port range (60001:60010)", true, false );
  FACT_CHECK( !single.bad_port, "not noted when no range was asked for" );

  /* ---- which server answered (REQ8/CMD-3) */
  Startup stock;
  FACT_CHECK( ( line( stock, std::string( STOCK_SERVER_LINE ) + "\r" ) == "taken" && stock.stock ), "the stock line, when the host was asked to choose: taken, and the server is stock" );
  Startup named;
  FACT_CHECK( ( line( named, STOCK_SERVER_LINE, false ) == "passed on" && !named.stock ), "the stock line when the user named the server: just chatter" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Startup );
}

FACTS_MAIN
