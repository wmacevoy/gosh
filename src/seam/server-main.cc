/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * gosh-server's main (design v2, 4.2). After the word "new" it takes gosh's words out of the
 * arguments, keeps what they ask for where the seam's hooks will find it, and then runs the
 * core's server with what is left: exactly the words a stock mosh-server would have been given.
 * Any other way of starting it goes to the core untouched.
 */

#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <string>
#include <vector>

#include "src/seam/session.h"
#include "vendor/retain/retain.hpp"
#include "src/seam/gosh-version.h"

#include "src/forwarding/stream/streamchild.h"
#include "src/seam/applications.h"
#include "src/seam/core-main.h"
#include "src/seam/hooks.h"
#include "src/seam/options.h"
#include "src/relay/relay.h"
#include "src/crypto/crypto.h"
#include "src/crypto/prng.h"
#include "src/meetup/master.h"
#include "src/meetup/meetup.h"
#include "src/meetup/ticket.h"

int main( int argc, char* argv[] )
{
  /* The session, for everything this calls: the seam's hooks recall it (session.cc). */
  Gosh::Session the_session;
  retain<Gosh::Session> as( &the_session );

  const std::string name( argc > 0 ? argv[0] : "gosh-server" );
  std::vector<std::string> args( argv + ( argc > 0 ? 1 : 0 ), argv + argc );

  /*
   * "gosh-server relay ADDRESS PORT" is not a server at all: it carries a session's datagrams
   * between an ssh connection and a server already running here, for when they cannot get here
   * by UDP (src/relay/relay.h; the fork's mosh-server.cc).
   */
  if ( args.size() == 3 && args[0] == "relay" ) {
    Crypto::disable_dumping_core(); /* (as the fork's server did before anything else) */
    return Network::relay_remote( args[1].c_str(), args[2].c_str() );
  }

  /* (REQ1/CMD-2) Which gosh this is, before the core says which mosh. */
  if ( args.size() == 1 && args[0] == "--version" ) {
    printf( "gosh-server, of gosh %s\n", GOSH_VERSION );
  }

  /* The fork's name for a meetup, dropped with no alias (requirements v16, Scope decisions): refused
     and named, not taken for the address of a server to start. */
  if ( !args.empty() && args[0] == "stun" ) {
    fprintf( stderr, "%s: \"stun\" is no longer a word of gosh's: a meetup is \"%s meetup PORT\".\n", name.c_str(),
             name.c_str() );
    return 1;
  }
  if ( !args.empty() && args[0] == "stun-key" ) {
    fprintf( stderr, "%s: \"stun-key\" is no longer a word of gosh's: a meetup's new key is \"%s meetup-key --new\".\n",
             name.c_str(), name.c_str() );
    return 1;
  }

  /*
   * A meetup, where two ends behind NATs find each other (src/meetup); a ticket for one session
   * to use it, given to whoever can run this here, over ssh; and a new master key, which ends
   * every ticket made under the old one.
   */
  if ( !args.empty() && ( args[0] == "meetup" || args[0] == "meetup-ticket" || args[0] == "meetup-key" ) ) {
    Crypto::disable_dumping_core();
    std::string master, error;
    if ( args.size() == 2 && args[0] == "meetup-key" && args[1] == "--new" ) {
      if ( !Gosh::meetup_master_new( error ) ) {
        fprintf( stderr, "%s meetup-key: %s\n", name.c_str(), error.c_str() );
        return 1;
      }
      return 0;
    }
    const bool serve = ( args.size() == 1 || args.size() == 2 ) && args[0] == "meetup";
    const bool ticket = args.size() == 1 && args[0] == "meetup-ticket";
    if ( !serve && !ticket ) {
      fprintf( stderr, "usage: %s meetup [PORT] | meetup-ticket | meetup-key --new\n", name.c_str() );
      return 1;
    }
    if ( !Gosh::meetup_master( master, error ) ) {
      fprintf( stderr, "%s %s: %s\n", name.c_str(), args[0].c_str(), error.c_str() );
      return 1;
    }
    if ( serve ) {
      /* (issue #1) the port the command takes when --meetup names none */
      return Network::meetup_serve( args.size() == 2 ? args[1].c_str() : "3478", master );
    }
    char random8[8];
    PRNG().fill( random8, sizeof( random8 ) );
    const Gosh::Ticket t = Gosh::make_ticket( master, std::string( random8, 8 ), time( NULL ) + Gosh::TICKET_LIFE );
    printf( "%s\n", Gosh::ticket_text( t ).c_str() );
    return 0;
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
    if ( options.stream_given && !Gosh::stream_words_valid( Network::stream_words( options.stream ) ) ) {
      fprintf( stderr, "%s: -X: not something that can be started\n", name.c_str() );
      return 1;
    }
    Gosh::session().client = false;
    Gosh::session().options = options;
    args.assign( 1, "new" );
    args.insert( args.end(), rest.begin(), rest.end() );
  }

  Gosh::CoreWords held;
  return Gosh::call_core_main( mosh_server_main, name, args, held );
}
