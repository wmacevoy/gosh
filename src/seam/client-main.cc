/*
    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

/*
 * gosh-client's main (design v2, 4.2). It takes gosh's words out of the arguments, keeps what
 * they ask for where the seam's hooks will find it, and then runs the core's client with what
 * is left: exactly the words a stock mosh-client would have been given.
 */

#include <cstdio>
#include <string>
#include <vector>

#include "src/seam/core-main.h"
#include "src/seam/hooks.h"
#include "src/seam/options.h"

int main( int argc, char* argv[] )
{
  const std::string name( argc > 0 ? argv[0] : "gosh-client" );
  const std::vector<std::string> args( argv + ( argc > 0 ? 1 : 0 ), argv + argc );

  Gosh::ForwardOptions options;
  std::vector<std::string> rest;
  std::string error;
  /* "#": the core's one option that takes a value (the original command line, which it ignores) */
  if ( !Gosh::take_forward_options( args, "#", true, options, rest, error ) ) {
    fprintf( stderr, "%s: %s\n", name.c_str(), error.c_str() );
    return 1;
  }

  Gosh::session().client = true;
  Gosh::session().options = options;
  return Gosh::call_core_main( mosh_client_main, name, rest );
}
