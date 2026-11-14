/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/*
 * gosh-server's main (design v2, 4.2). After the word "new" it takes gosh's words out of the
 * arguments, keeps what they ask for where the seam's hooks will find it, and then runs the
 * core's server with what is left: exactly the words a stock mosh-server would have been given.
 * Any other way of starting it goes to the core untouched.
 */

#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#include "src/forwarding/stream/streamchild.h"
#include "src/seam/core-main.h"
#include "src/seam/hooks.h"
#include "src/seam/options.h"
#include "src/relay/relay.h"
#include "src/crypto/crypto.h"

int main( int argc, char* argv[] )
{
  const std::string name( argc > 0 ? argv[0] : "gosh-server" );
  std::vector<std::string> args( argv + ( argc > 0 ? 1 : 0 ), argv + argc );

  /*
   * "gosh-server relay ADDRESS PORT" is not a server at all: it carries a session's datagrams
   * between an ssh connection and a server already running here, for when they cannot get here
   * by UDP (src/relay/relay.h; the fork's mosh-server.cc).
   */
  if ( args.size() == 3 && args[0] == "relay" ) {
    Crypto::disable_dumping_core(); /* (as the fork's server did before anything else) */
    /* Path-finding is not landed (queue item 8): the remote relay does not offer it. This line goes when it lands. */
    setenv( "MOSH_PUNCH", "never", 1 );
    return Network::relay_remote( args[1].c_str(), args[2].c_str() );
  }

  if ( !args.empty() && args[0] == "new" ) {
    Gosh::ForwardOptions options;
    std::vector<std::string> rest;
    std::string error;
    /* "@ipcl": the core's options that take a value, so that a value is not read as an option */
    if ( !Gosh::take_forward_options( std::vector<std::string>( args.begin() + 1, args.end() ), "@ipcl", false, false,
                                      options, rest, error ) ) {
      fprintf( stderr, "%s: %s\n", name.c_str(), error.c_str() );
      return 1;
    }
    if ( options.stream_given && !Network::stream_words_valid( Network::stream_words( options.stream ) ) ) {
      fprintf( stderr, "%s: -X: not something that can be started\n", name.c_str() );
      return 1;
    }
    Gosh::session().client = false;
    Gosh::session().options = options;
    args.assign( 1, "new" );
    args.insert( args.end(), rest.begin(), rest.end() );
  }

  return Gosh::call_core_main( mosh_server_main, name, args );
}
