/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#include "src/command/startup.h"

#include <vector>

#include "src/command/options.h"
#include "src/command/stock.h"
#include "src/command/words.h"

bool Gosh::Command::startup_line( Startup& st, const std::string& raw, bool pick_server, bool port_range, const std::string& name )
{
  const std::string line = strip_end( raw );
  const Words words = shell_words( line );
  if ( line.compare( 0, 8, "MOSH IP " ) == 0 ) {
    if ( !st.ip.empty() ) {
      throw Failure( name + " error: detected attempt to redefine MOSH IP." );
    }
    if ( words.size() != 3 ) {
      throw Failure( "Bad MOSH IP string: " + line );
    }
    st.ip = words[2];
  } else if ( line.compare( 0, 20, "MOSH SSH_CONNECTION " ) == 0 ) {
    if ( words.size() != 6 ) {
      throw Failure( "Bad MOSH SSH_CONNECTION string: " + line );
    }
    st.ssh_ip = words[4];
  } else if ( line.compare( 0, 13, "MOSH CONNECT " ) == 0 ) {
    if ( words.size() != 4 || !all_digits( words[2] ) || words[3].size() != 22
         || words[3].find_first_not_of( "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/+" )
              != std::string::npos ) {
      throw Failure( "Bad MOSH CONNECT string: " + line );
    }
    st.port = words[2];
    st.key = words[3];
    st.done = true;
  } else if ( pick_server && line == STOCK_SERVER_LINE ) { /* (gosh, REQ8/CMD-3) */
    st.stock = true;
  } else {
    if ( port_range && line.find( "Bad UDP port" ) != std::string::npos ) {
      st.bad_port = true;
    }
    return false;
  }
  return true;
}

std::string Gosh::Command::startup_address( Startup& st, const std::string& name, const std::string& userhost )
{
  if ( !st.ip.empty() ) {
    return "";
  }
  if ( st.ssh_ip.empty() ) {
    throw Failure( name + ": Did not find remote IP address (is SSH ProxyCommand disabled?)." );
  }
  st.ip = st.ssh_ip;
  return name + ": Using remote IP address " + st.ssh_ip + " from $SSH_CONNECTION for hostname " + userhost;
}

void Gosh::Command::startup_key( const Startup& st, const std::string& name )
{
  if ( st.key.empty() || st.port.empty() ) {
    if ( st.bad_port ) {
      throw Failure( name + ": Server does not support UDP port range option." );
    }
    throw Failure( name + ": Did not find the server's startup message. (Is gosh installed there? https://wmacevoy.github.io/gosh/USING.html)" );
  }
}
