/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

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

namespace Gosh::Seam {

/* The words a core's main is called with, and the pointers to them it is handed. Whoever calls
   the core's main makes one and keeps it for as long as the core runs: the core keeps pointers into it. */
struct CoreWords
{
  std::vector<std::string> kept;
  std::vector<char*> argv;
};

/*
 * Calls one of the core's mains with `name` as its argv[0] and `words` after it.
 * The words live in `held`, which the caller keeps for as long as the core runs: the core keeps pointers into them.
 */
inline int call_core_main( int ( *core_main )( int, char*[] ), const std::string& name, const std::vector<std::string>& words, CoreWords& held )
{
  held.kept.clear();
  held.kept.push_back( name );
  held.kept.insert( held.kept.end(), words.begin(), words.end() );
  held.argv.clear();
  for ( size_t i = 0; i < held.kept.size(); i++ ) {
    held.argv.push_back( const_cast<char*>( held.kept[i].c_str() ) );
  }
  held.argv.push_back( NULL );
  return core_main( static_cast<int>( held.kept.size() ), held.argv.data() );
}

}

#endif
