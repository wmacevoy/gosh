/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-1, REQ1/CP-2: fuzzes the sending end of a file copy (filecopy.h): the input is what the
 * receiving end might say (which files it wants, from where, what became of them). The sender acts
 * on another machine's word too: where to start a file, which file, how many rounds.
 *
 * Held to: it writes nothing where it reads from (the source tree, every name, size, time and
 * content, is the same after each input); and all it says is whole messages of the thirteen
 * kinds, none longer than MAX_MESSAGE.
 *
 * New in gosh (the review of item 21, 2026-11-16: the sending end had no fuzzer). Its scratch is
 * under TMPDIR, and is cleared after a crash, as filecopy-fuzzer's is.
 *
 * Input: what the receiver says, as it would arrive (messages.h's framing).
 */

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/copy/filecopy.h"
#include "src/copy/messages.h"
#include "src/seam/fuzz-replay.h"

namespace {

const char PREFIX[] = "gosh-copy-send-fuzz-";
std::string root, source;

void require( bool ok )
{
  if ( !ok ) {
    abort();
  }
}

void empty( const std::string& path, bool and_remove )
{
  struct stat st;
  if ( lstat( path.c_str(), &st ) < 0 ) {
    return;
  }
  if ( S_ISDIR( st.st_mode ) ) {
    chmod( path.c_str(), 0700 );
    if ( DIR* dir = opendir( path.c_str() ) ) {
      while ( struct dirent* d = readdir( dir ) ) {
        const std::string name = d->d_name;
        if ( name != "." && name != ".." ) {
          empty( path + "/" + name, true );
        }
      }
      closedir( dir );
    }
    if ( and_remove ) {
      rmdir( path.c_str() );
    }
  } else {
    unlink( path.c_str() );
  }
}

void cleanup( void )
{
  if ( !root.empty() ) {
    empty( root, true );
  }
}

void clear_left( const std::string& tmp )
{
  if ( DIR* dir = opendir( tmp.c_str() ) ) {
    while ( struct dirent* d = readdir( dir ) ) {
      const std::string name = d->d_name;
      if ( name.compare( 0, sizeof( PREFIX ) - 1, PREFIX ) != 0 ) {
        continue;
      }
      const long pid = atol( name.c_str() + sizeof( PREFIX ) - 1 );
      struct stat st;
      if ( pid > 0 && kill( static_cast<pid_t>( pid ), 0 ) < 0 && errno == ESRCH
           && lstat( ( tmp + "/" + name ).c_str(), &st ) == 0 && st.st_uid == getuid() ) {
        empty( tmp + "/" + name, true );
      }
    }
    closedir( dir );
  }
}

std::string contents( const std::string& path )
{
  std::string s;
  const int fd = open( path.c_str(), O_RDONLY );
  if ( fd < 0 ) {
    return s;
  }
  char buf[65536];
  ssize_t n;
  while ( ( n = read( fd, buf, sizeof( buf ) ) ) > 0 ) {
    s.append( buf, n );
  }
  close( fd );
  return s;
}

/* The source tree: each name's kind, size, time and content. */
void snapshot( const std::string& path, std::map<std::string, std::string>& seen )
{
  struct stat st;
  if ( lstat( path.c_str(), &st ) < 0 ) {
    return;
  }
  char line[256];
  snprintf( line, sizeof( line ), "%o %lld %lld.%09ld", static_cast<unsigned>( st.st_mode ),
            static_cast<long long>( st.st_size ), static_cast<long long>( st.st_mtim.tv_sec ), st.st_mtim.tv_nsec );
  seen[path] = line + ( S_ISREG( st.st_mode ) ? " " + contents( path ) : std::string() );
  if ( S_ISDIR( st.st_mode ) ) {
    if ( DIR* dir = opendir( path.c_str() ) ) {
      while ( struct dirent* d = readdir( dir ) ) {
        const std::string name = d->d_name;
        if ( name != "." && name != ".." ) {
          snapshot( path + "/" + name, seen );
        }
      }
      closedir( dir );
    }
  }
}

void put( const std::string& path, const std::string& content )
{
  const int fd = open( path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600 );
  require( fd >= 0 && write( fd, content.data(), content.size() ) == static_cast<ssize_t>( content.size() ) );
  close( fd );
}

void make_tree( void )
{
  const char* t = getenv( "TMPDIR" );
  const std::string tmp = t && *t ? t : "/tmp";
  clear_left( tmp );
  std::string name = tmp + "/" + PREFIX + std::to_string( getpid() ) + "-XXXXXX";
  require( mkdtemp( &name[0] ) != NULL );
  root = name;
  source = root + "/src";
  mkdir( source.c_str(), 0700 );
  mkdir( ( source + "/sub" ).c_str(), 0700 );
  put( source + "/a", "one" );
  put( source + "/empty", "" );
  std::string big;
  for ( int i = 0; i < 70000; i++ ) {
    big.push_back( static_cast<char>( i * 7 ) );
  }
  put( source + "/big", big );
  put( source + "/sub/c", "three" );
  atexit( cleanup );
  GoshFuzzReplay::on_crash() = cleanup;
}

}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  if ( root.empty() ) {
    make_tree();
  }
  const std::string said = root + "/said", out = root + "/out";
  put( said, std::string( reinterpret_cast<const char*>( data ), size ) );

  std::map<std::string, std::string> before, after;
  snapshot( source, before );

  FileCopy::Options options;
  options.sender = true;
  options.recursive = true;
  options.quiet = true;
  options.paths.push_back( source );

  const int in = open( said.c_str(), O_RDONLY );
  const int to = open( out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600 );
  require( in >= 0 && to >= 0 );
  FileCopy::run( options, in, to );
  close( in );
  close( to );

  /* it wrote nothing where it reads from */
  snapshot( source, after );
  require( before == after );

  /* and all it said is whole messages of the kinds there are */
  std::string spoken = contents( out );
  uint8_t type;
  std::string body;
  while ( FileCopy::unframe( spoken, type, body ) ) {
    require( type >= FileCopy::HELLO && type <= FileCopy::BYE );
  }
  require( spoken.empty() );
  return 0;
}
