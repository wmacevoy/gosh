/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * gosh-client's main (design v2, 4.2). gosh-client is several programs under one name:
 *
 *   the command   run as gosh or gocp, it first logs in by ssh and starts the server
 *                 (src/command), and then carries on, in this same process, as
 *   the client    of the session that made. It takes gosh's words out of the arguments, keeps
 *                 what they ask for where the seam's hooks will find it, and runs the core's
 *                 client with what is left: exactly the words a stock mosh-client would be given.
 *   ssh's proxy   run by ssh as its ProxyCommand, with a first word only the command gives it,
 *                 it is the command's way of learning which address ssh reached.
 */

#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#include "src/seam/imp/session.h"
#include "vendor/retain/retain.hpp"
#include "src/command/moshwrapper.h"
#include "src/forwarding/streamchild.h"
#include "src/seam/imp/applications.h"
#include "src/seam/core-main.h"
#include "src/seam/gosh-version.h"
#include "src/seam/hooks.h"
#include "src/seam/imp/options.h"

int main( int argc, char* argv[] )
{
  /* The session, for everything this calls: the seam's hooks recall it (session.cc). */
  Gosh::Seam::Imp::Session the_session;
  retain<Gosh::Seam::Imp::Session> as( &the_session );

  const std::string name( argc > 0 ? argv[0] : "gosh-client" );
  std::vector<std::string> args( argv + ( argc > 0 ? 1 : 0 ), argv + argc );

  /* (the word is the fork's, and the command writes it: src/command) */
  if ( args.size() == 4 && args[0] == "--mosh-fake-proxy" ) {
    return Gosh::Command::fake_proxy( args[1], args[2], args[3] );
  }

  std::string called;
  size_t skip = 0;
  const Gosh::Seam::Imp::Role role = Gosh::Seam::Imp::which_role( name, args, called, skip );
  if ( role != Gosh::Seam::Imp::THE_CLIENT ) {
    std::vector<std::string> client_words; /* the client's name, then its arguments */
    int status = 1;
    if ( !Gosh::Command::run( std::vector<std::string>( args.begin() + skip, args.end() ), role == Gosh::Seam::Imp::THE_COPY, called,
                            name, print_colorcount, client_words, status ) ) {
      return status; /* it has said why, or has done all that was asked (--help, --version) */
    }
    args.assign( client_words.begin() + ( client_words.empty() ? 0 : 1 ), client_words.end() );
  }

  /* (REQ1/CMD-2) Which gosh this is, before the core says which mosh. */
  if ( role == Gosh::Seam::Imp::THE_CLIENT && args.size() == 1 && args[0] == "--version" ) {
    printf( "gosh-client, of gosh %s\n", GOSH_VERSION );
  }

  Gosh::Seam::Imp::ForwardOptions options;
  std::vector<std::string> rest;
  std::string error;
  /* "#": the core's one option that takes a value (the original command line, which it ignores) */
  if ( !Gosh::Seam::Imp::take_forward_options( args, "#", true, true, options, rest, error ) ) {
    fprintf( stderr, "%s: %s\n", name.c_str(), error.c_str() );
    return 1;
  }

  if ( options.stream_given && !Gosh::Seam::Imp::stream_words_valid( Gosh::Forwarding::stream_words( options.stream ) ) ) {
    fprintf( stderr, "%s: -X: not something that can be started\n", name.c_str() );
    return 1;
  }

  Gosh::Seam::Imp::session().client = true;
  Gosh::Seam::Imp::session().options = options;

  /*
   * How to reach the server if UDP will not get there (the fork's mosh-client.cc): a command
   * that starts the remote relay on it, through ssh as a rule, given as its words separated by
   * the character 0x1F so that no shell is needed to take it apart; and whether to use it when
   * needed or from the start. The command writes both (src/command).
   */
  std::vector<std::string> relay_command;
  if ( const char* command = getenv( "GOSH_RELAY_COMMAND" ) ) {
    std::string word;
    for ( const char* c = command; *c; c++ ) {
      if ( *c == '\x1f' ) {
        relay_command.push_back( word );
        word.clear();
      } else {
        word.push_back( *c );
      }
    }
    relay_command.push_back( word );
  }
  Gosh::Seam::Imp::session().relay.command = relay_command;
  Gosh::Seam::Imp::session().relay.mode = Gosh::Relay::relay_mode( getenv( "GOSH_RELAY" ) ? getenv( "GOSH_RELAY" ) : "auto", !relay_command.empty() );
  /* where the server is: the two words the core's own reading will take for it, wherever they stand */
  const std::vector<std::string> where = Gosh::Seam::Imp::operands( rest, "#" );
  if ( where.size() >= 2 ) {
    Gosh::Seam::Imp::session().relay.server_ip = where[0];
    Gosh::Seam::Imp::session().relay.server_port = where[1];
  }

  Gosh::Seam::CoreWords held;
  return Gosh::Seam::call_core_main( mosh_client_main, name, rest, held );
}
