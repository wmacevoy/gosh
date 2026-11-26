/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ14/SPEED-1: the byte queue (bytes.h) holds what a string would. */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/forwarding/imp/tcp/bytes.h"
#include "vendor/facts/facts.h"



static std::string pattern( size_t length )
{
  std::string s( length, 0 );
  for ( size_t i = 0; i < length; i++ ) {
    s[i] = static_cast<char>( ( i * 2654435761u ) >> 13 );
  }
  return s;
}
FACTS( Bytes )
{
  Gosh::Forwarding::Imp::Bytes queue;
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
  FACT_CHECK( ( agree && most > 100000 ), "the byte queue holds what a string would, through 200,000 additions and removals" );
}
FACTS( PartAndTidying )
{
  Gosh::Forwarding::Imp::Bytes queue;
  const std::string source = pattern( 1000 );
  queue.append( source, 100, 250 );
  FACT_CHECK( ( queue.size() == 250 && std::string( queue.data(), queue.size() ) == source.substr( 100, 250 ) ), "part of a string is added: the 250 bytes from its 100th" );
  queue.append( source, 0, 0 );
  FACT_CHECK( queue.size() == 250, "none of a string adds nothing" );

  /* 1 MB waits; it is taken from the front 1,000 bytes at a time while as much is added: what is kept stays near what waits. */
  Gosh::Forwarding::Imp::Bytes steady;
  const std::string megabyte = pattern( 1024 * 1024 );
  steady.append( megabyte.data(), megabyte.size() );
  bool right = true;
  for ( int i = 0; i < 5000; i++ ) {
    steady.drop( 1000 );
    steady.append( megabyte.data(), 1000 );
    right = right && steady.size() == megabyte.size();
  }
  FACT_CHECK( right, "5 MB through a queue of 1 MB, taken and added a thousand bytes at a time: 1 MB waits throughout" );
  /* what waits is the end of: the megabyte, then its first thousand bytes 5,000 times; 5,000,000 bytes in, that is 424 bytes into a copy */
  FACT_CHECK( ( std::string( steady.data(), 1000 ) == megabyte.substr( 424, 576 ) + megabyte.substr( 0, 424 )
           && steady.substr( steady.size() - 1000, 1000 ) == megabyte.substr( 0, 1000 ) ), "and what waits is the right bytes" );
}
/* REQ14/SPEED-1: the byte queue */
FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Bytes );
  FACTS_REGISTER( PartAndTidying );
}

FACTS_MAIN
