/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-6: fuzzes the receiving end of a file copy (filecopy.h): the input is what the sending
 * end might say. It is the end that writes files where it is told to, by another machine, so it
 * is the end that must not be led astray: "a receiving end writes only inside its destination,
 * whatever names the sending end gives it."
 *
 * Held to that: the destination sits DEPTH directories down a scratch tree of the fuzzer's own,
 * with a file at every level, so that a name that climbs out with up to DEPTH ".." still lands
 * inside the tree; and the whole tree outside the destination (every name, kind, size, time,
 * content and link) is the same after each input as before. (A climb of more than DEPTH leaves
 * the tree, and is not seen; nor is a write elsewhere by an absolute name, which the receiver
 * would have to be told outright.)
 *
 * (gosh) The fork's src/fuzz/file_copy_fuzzer.cc (mosh-1.4.0-pf.7), changed. Until the review of
 * item 21 (2026-11-16) it checked only that one file beside the destination kept its size, so a
 * write to "../other" passed. Its scratch is under TMPDIR now; one that a crashed run left is
 * cleared by the next. Its corpus is the fork's, with the reviewer's climb-elsewhere.
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
#include "src/seam/fuzz-replay.h"

namespace {

const int DEPTH = 12;
const char PREFIX[] = "gosh-copy-fuzz-";

std::string root, dest;

void empty( const std::string& path, bool and_remove )
{
  struct stat st;
  if ( lstat( path.c_str(), &st ) < 0 ) {
    return;
  }
  if ( S_ISDIR( st.st_mode ) ) {
    chmod( path.c_str(), 0700 ); /* (what the other end said may have closed it) */
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

/* Scratch trees of runs that are no longer running (a crash under libFuzzer cleans nothing). */
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

uint64_t fnv( const std::string& path )
{
  uint64_t h = 14695981039346656037ULL;
  const int fd = open( path.c_str(), O_RDONLY );
  if ( fd < 0 ) {
    return 0;
  }
  char buf[4096];
  ssize_t n;
  while ( ( n = read( fd, buf, sizeof( buf ) ) ) > 0 ) {
    for ( ssize_t i = 0; i < n; i++ ) {
      h = ( h ^ static_cast<unsigned char>( buf[i] ) ) * 1099511628211ULL;
    }
  }
  close( fd );
  return h;
}

/* Everything in the tree but the destination: each name's kind, size, times, content or link. */
void snapshot( const std::string& path, std::map<std::string, std::string>& seen )
{
  if ( path == dest ) {
    return;
  }
  struct stat st;
  if ( lstat( path.c_str(), &st ) < 0 ) {
    return;
  }
  char line[256];
  snprintf( line, sizeof( line ), "%o %lld %lld.%09ld", static_cast<unsigned>( st.st_mode ),
            static_cast<long long>( st.st_size ), static_cast<long long>( st.st_mtim.tv_sec ), st.st_mtim.tv_nsec );
  std::string what = line;
  if ( S_ISREG( st.st_mode ) ) {
    what += " " + std::to_string( fnv( path ) );
  } else if ( S_ISLNK( st.st_mode ) ) {
    char to[4096];
    const ssize_t n = readlink( path.c_str(), to, sizeof( to ) );
    what += " -> " + std::string( to, n > 0 ? n : 0 );
  }
  seen[path.substr( root.size() )] = what;
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
  if ( fd < 0 || write( fd, content.data(), content.size() ) != static_cast<ssize_t>( content.size() ) ) {
    abort();
  }
  close( fd );
}

void make_tree( void )
{
  const char* t = getenv( "TMPDIR" );
  const std::string tmp = t && *t ? t : "/tmp";
  clear_left( tmp );
  std::string name = tmp + "/" + PREFIX + std::to_string( getpid() ) + "-XXXXXX";
  if ( !mkdtemp( &name[0] ) ) {
    abort();
  }
  root = name;
  std::string at = root;
  for ( int level = 0; level < DEPTH; level++ ) {
    put( at + "/said", "level " + std::to_string( level ) ); /* each level has the name the climb tries */
    put( at + "/other", "and another" );
    at += "/d";
    mkdir( at.c_str(), 0700 );
  }
  dest = at + "/dest";
  mkdir( dest.c_str(), 0700 );
  atexit( cleanup );
  GoshFuzzReplay::on_crash() = cleanup;
}

}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  if ( root.empty() ) {
    make_tree();
  }
  if ( size < 1 ) {
    return 0;
  }

  /* what the other end said, as something to read from: outside the destination, in the tree */
  const std::string said = root + "/said-now";
  put( said, std::string( reinterpret_cast<const char*>( data + 1 ), size - 1 ) );

  std::map<std::string, std::string> before, after;
  snapshot( root, before );

  Gosh::Copy::Options options;
  options.sender = false;
  options.local = false;
  options.remove = data[0] & 1;
  options.paths.push_back( ( data[0] & 2 ) ? dest + "/" : dest + "/as" );

  const int in = open( said.c_str(), O_RDONLY );
  const int out = open( "/dev/null", O_WRONLY );
  if ( in < 0 || out < 0 ) {
    abort();
  }
  Gosh::Copy::run( options, in, out );
  close( in );
  close( out );

  /* whatever it was told, nothing outside the destination changed */
  snapshot( root, after );
  if ( before != after ) {
    for ( std::map<std::string, std::string>::const_iterator i = after.begin(); i != after.end(); i++ ) {
      if ( !before.count( i->first ) || before[i->first] != i->second ) {
        fprintf( stderr, "filecopy-fuzzer: written outside the destination: %s\n", i->first.c_str() );
      }
    }
    abort();
  }
  empty( dest, false );
  return 0;
}
