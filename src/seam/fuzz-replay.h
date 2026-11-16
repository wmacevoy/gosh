/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * A fuzzer's main for the ordinary compiler: it feeds each file of the fuzzer's corpus to
 * LLVMFuzzerTestOneInput, and then a fixed number of changes of each, made by a generator with a
 * fixed seed; so that the check (`make check`) replays every corpus and searches a little around
 * it without clang, the same way every time, and an input that once crashed a part crashes the
 * check (queue item 21). Built with libFuzzer (-DGOSH_LIBFUZZER, as CI does), this is left out
 * and libFuzzer's main runs instead.
 *
 * The corpus is the directory named for the program with "-corpus" after it, beside it (as
 * `tcp/tcpinudp-fuzzer` has `tcp/tcpinudp-fuzzer-corpus`); or the files and directories named
 * as arguments. No input found is a failure, so that a corpus left behind by a move is not a pass.
 * An input that crashes it is kept beside it, named for it with "-crash" after, and said.
 *
 * Here, beside the parts, because the build copies only src/'s directories into the core's tree,
 * and every part's fuzzer includes it.
 */

#ifndef GOSH_SEAM_FUZZ_REPLAY_H
#define GOSH_SEAM_FUZZ_REPLAY_H

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size );

namespace GoshFuzzReplay {
/* What a fuzzer wants done before a crash ends it, such as removing its scratch directory: set by
   the fuzzer, called by the replay's crash handler. (Under libFuzzer nothing calls it: a fuzzer
   that keeps scratch also clears what earlier runs left.) */
typedef void ( *Hook )( void );
inline Hook& on_crash( void )
{
  static Hook hook = 0;
  return hook;
}
}

#ifndef GOSH_LIBFUZZER

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* The input being tried, and where to keep it if it crashes. */
const std::string* trying = NULL;
char kept[4096];

inline void say( const char* s, size_t n )
{
  ssize_t ignored = write( STDOUT_FILENO, s, n );
  ( void )ignored;
}

inline void keep_and_die( int signal )
{
  if ( on_crash() ) {
    on_crash()();
  }
  if ( trying ) {
    const int fd = open( kept, O_WRONLY | O_CREAT | O_TRUNC, 0600 );
    if ( fd >= 0 ) {
      ssize_t ignored = write( fd, trying->data(), trying->size() );
      ( void )ignored;
      close( fd );
    }
    const char said[] = "FAIL: the input that crashed it is kept in ";
    say( said, sizeof( said ) - 1 );
    say( kept, strlen( kept ) );
    say( "\n", 1 );
  }
  ::signal( signal, SIG_DFL );
  raise( signal );
}

inline void run( const std::string& input )
{
  trying = &input;
  LLVMFuzzerTestOneInput( reinterpret_cast<const uint8_t*>( input.data() ), input.size() );
  trying = NULL;
}

/* xorshift32: a generator with a fixed seed, so that every run is the same run. */
inline uint32_t next( uint32_t& state )
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

/* One change of libFuzzer's kinds: a byte set, a bit flipped, a byte put in, bytes cut, a run repeated. */
inline void mutate( std::string& s, uint32_t& state )
{
  const uint32_t r = next( state );
  const size_t at = s.empty() ? 0 : next( state ) % s.size();
  const char b = static_cast<char>( next( state ) & 0xFF );
  switch ( r % 6 ) {
    case 0:
      if ( !s.empty() ) {
        s[at] = b;
      }
      break;
    case 1:
      if ( !s.empty() ) {
        s[at] = static_cast<char>( s[at] ^ ( 1 << ( r >> 8 ) % 8 ) );
      }
      break;
    case 2:
      s.insert( s.begin() + at, b );
      break;
    case 3:
      s.erase( at, 1 + ( r >> 8 ) % 8 );
      break;
    case 4: /* a boundary value, where lengths and counts go wrong */
      if ( !s.empty() ) {
        const char edges[] = { 0, 1, 0x7F, static_cast<char>( 0x80 ), static_cast<char>( 0xFF ) };
        s[at] = edges[( r >> 8 ) % 5];
      }
      break;
    default:
      if ( s.size() < 4096 ) {
        s.insert( at, s.substr( at, 1 + ( r >> 8 ) % 32 ) );
      }
      break;
  }
}

}

int main( int argc, char* argv[] )
{
  using namespace GoshFuzzReplay;
  std::vector<std::string> files;
  if ( argc > 1 ) {
    for ( int i = 1; i < argc; i++ ) {
      gather( argv[i], files );
    }
  } else {
    gather( std::string( argv[0] ) + "-corpus", files );
  }
  if ( files.empty() ) {
    printf( "FAIL: %s: no corpus to replay\n", argv[0] );
    return 1;
  }
  snprintf( kept, sizeof( kept ), "%s-crash", argv[0] );
  const int fatal[] = { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL };
  for ( size_t i = 0; i < sizeof( fatal ) / sizeof( fatal[0] ); i++ ) {
    signal( fatal[i], keep_and_die );
  }
  fflush( stdout );

  /*
   * Each input as it is, and then MUTATIONS changes of it: a search of libFuzzer's kind, smaller
   * and always the same (the generator's seed is fixed), so that the check finds what a moment's
   * fuzzing would, and a failure can be had again by running the program again.
   */
  const unsigned MUTATIONS = 200;
  uint32_t state = 2463534242u;
  for ( size_t i = 0; i < files.size(); i++ ) {
    std::ifstream in( files[i].c_str(), std::ios::binary );
    const std::string bytes( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    run( bytes );
    for ( unsigned m = 0; m < MUTATIONS; m++ ) {
      std::string changed = bytes;
      const unsigned changes = 1 + next( state ) % 4;
      for ( unsigned c = 0; c < changes; c++ ) {
        mutate( changed, state );
      }
      run( changed );
    }
  }
  printf( "PASS: %s: %u inputs replayed, each changed %u ways\n", argv[0], static_cast<unsigned>( files.size() ),
          MUTATIONS );
  return 0;
}

#endif

#endif
