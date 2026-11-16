/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * A fuzzer's main for the ordinary compiler: it feeds each file of the fuzzer's corpus to
 * LLVMFuzzerTestOneInput once, so that the check (`make check`) replays every corpus without
 * clang, and an input that once crashed a part crashes the check (queue item 21). Built with
 * libFuzzer (-DGOSH_LIBFUZZER, as CI does), this is left out and libFuzzer's main runs instead.
 *
 * The corpus is the directory named for the program with "-corpus" after it, beside it (as
 * `tcp/tcpinudp-fuzzer` has `tcp/tcpinudp-fuzzer-corpus`); or the files and directories named
 * as arguments. No input found is a failure, so that a corpus left behind by a move is not a pass.
 *
 * Here, beside the parts, because the build copies only src/'s directories into the core's tree,
 * and every part's fuzzer includes it.
 */

#ifndef GOSH_SEAM_FUZZ_REPLAY_H
#define GOSH_SEAM_FUZZ_REPLAY_H

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size );

#ifndef GOSH_LIBFUZZER

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

namespace GoshFuzzReplay {

inline void gather( const std::string& path, std::vector<std::string>& files )
{
  struct stat st;
  if ( stat( path.c_str(), &st ) != 0 ) {
    return;
  }
  if ( !S_ISDIR( st.st_mode ) ) {
    files.push_back( path );
    return;
  }
  std::vector<std::string> names;
  if ( DIR* dir = opendir( path.c_str() ) ) {
    while ( struct dirent* d = readdir( dir ) ) {
      const std::string name = d->d_name;
      if ( name != "." && name != ".." ) {
        names.push_back( path + "/" + name );
      }
    }
    closedir( dir );
  }
  std::sort( names.begin(), names.end() );
  for ( size_t i = 0; i < names.size(); i++ ) {
    gather( names[i], files );
  }
}

}

int main( int argc, char* argv[] )
{
  std::vector<std::string> files;
  if ( argc > 1 ) {
    for ( int i = 1; i < argc; i++ ) {
      GoshFuzzReplay::gather( argv[i], files );
    }
  } else {
    GoshFuzzReplay::gather( std::string( argv[0] ) + "-corpus", files );
  }
  if ( files.empty() ) {
    printf( "FAIL: %s: no corpus to replay\n", argv[0] );
    return 1;
  }
  for ( size_t i = 0; i < files.size(); i++ ) {
    std::ifstream in( files[i].c_str(), std::ios::binary );
    const std::string bytes( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    LLVMFuzzerTestOneInput( reinterpret_cast<const uint8_t*>( bytes.data() ), bytes.size() );
  }
  printf( "PASS: %s: %u inputs replayed\n", argv[0], static_cast<unsigned>( files.size() ) );
  return 0;
}

#endif

#endif
