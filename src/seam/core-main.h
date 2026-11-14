/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).
*/

#ifndef GOSH_SEAM_CORE_MAIN_HPP
#define GOSH_SEAM_CORE_MAIN_HPP

#include <string>
#include <vector>

/*
 * The core's own mains, under the names the seam's patch gives them, so that gosh's main can
 * run first and call them with only the words a stock mosh would have been given (design v2, 4.2).
 */
int mosh_client_main( int argc, char* argv[] );
int mosh_server_main( int argc, char* argv[] );

/* The core's own: prints the number of colors the terminal has on standard output (mosh-client -c). */
void print_colorcount( void );

namespace Gosh {

/*
 * Calls one of the core's mains with `name` as its argv[0] and `words` after it.
 * The words are kept for the life of the process: the core keeps pointers into them.
 */
inline int call_core_main( int ( *core_main )( int, char*[] ), const std::string& name, const std::vector<std::string>& words )
{
  static std::vector<std::string> kept;
  static std::vector<char*> argv;
  kept.clear();
  kept.push_back( name );
  kept.insert( kept.end(), words.begin(), words.end() );
  argv.clear();
  for ( size_t i = 0; i < kept.size(); i++ ) {
    argv.push_back( const_cast<char*>( kept[i].c_str() ) );
  }
  argv.push_back( NULL );
  return core_main( static_cast<int>( kept.size() ), argv.data() );
}

}

#endif
