/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ14/SPEED-1: the byte queue (bytes.h) holds what a string would. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/tcp/bytes.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-90s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static std::string pattern( size_t length )
{
  std::string s( length, 0 );
  for ( size_t i = 0; i < length; i++ ) {
    s[i] = static_cast<char>( ( i * 2654435761u ) >> 13 );
  }
  return s;
}

/* Bytes behaves as a string that is appended to at the end and taken from at the front. */
static void test_bytes( void )
{
  Gosh::Bytes queue;
  std::string model;
  uint32_t state = 12345;
  bool agree = true;
  size_t most = 0;
  for ( int i = 0; i < 200000 && agree; i++ ) {
    state = state * 1664525 + 1013904223;
    const size_t n = ( state >> 8 ) % 3000;
    if ( ( state >> 28 ) % 2 == 0 && model.size() < 1024 * 1024 ) {
      const std::string more = pattern( n ).substr( 0, n );
      queue.append( more.data(), more.size() );
      model += more;
    } else {
      queue.drop( n );
      model.erase( 0, std::min( n, model.size() ) );
    }
    most = std::max( most, model.size() );
    agree = queue.size() == model.size() && queue.empty() == model.empty()
            && std::string( queue.data(), queue.size() ) == model
            && queue.substr( n % ( model.size() + 1 ), 100 ) == model.substr( n % ( model.size() + 1 ), 100 );
  }
  check( "the byte queue holds what a string would, through 200,000 additions and removals", agree && most > 100000 );
}

int main()
{
  test_bytes();

  if ( failures ) {
    printf( "FAIL REQ14/SPEED-1: %d checks of the byte queue failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ14/SPEED-1: the byte queue\n" );
  return EXIT_SUCCESS;
}
