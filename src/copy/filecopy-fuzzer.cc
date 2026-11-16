/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-6: fuzzes the receiving end of a file copy (filecopy.h): the input is what the sending
 * end might say. It is the end that writes files where it is told to, by another machine, so it
 * is the end that must not be led astray. Everything it does should stay inside its destination:
 * a file beside the destination, which the input is read from, must be there unchanged after.
 *
 * (gosh) The fork's src/fuzz/file_copy_fuzzer.cc (mosh-1.4.0-pf.7); its includes and its scratch
 * directory's name changed. Its corpus is the fork's ("climb" is the one that tries to leave).
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/copy/filecopy.h"
#include "src/seam/fuzz-replay.h"

static std::string scratch;

static void empty( const std::string& path, bool and_remove )
{
  struct stat st;
  if ( lstat( path.c_str(), &st ) < 0 ) {
    return;
  }
  if ( S_ISDIR( st.st_mode ) ) {
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

static void cleanup( void )
{
  empty( scratch, true );
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
  if ( scratch.empty() ) {
    char name[] = "/tmp/gosh-copy-fuzz-XXXXXX";
    if ( !mkdtemp( name ) ) {
      abort();
    }
    scratch = name;
    mkdir( ( scratch + "/dest" ).c_str(), 0700 );
    atexit( cleanup );
  }
  if ( size < 1 ) {
    return 0;
  }

  /* what the other end said, as something to read from */
  const std::string said = scratch + "/said";
  int fd = open( said.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600 );
  if ( fd < 0 || write( fd, data + 1, size - 1 ) != static_cast<ssize_t>( size - 1 ) ) {
    abort();
  }
  close( fd );

  FileCopy::Options options;
  options.sender = false;
  options.local = false;
  options.remove = data[0] & 1;
  options.paths.push_back( ( data[0] & 2 ) ? scratch + "/dest/" : scratch + "/dest/as" );

  const int in = open( said.c_str(), O_RDONLY );
  const int out = open( "/dev/null", O_WRONLY );
  if ( in < 0 || out < 0 ) {
    abort();
  }
  FileCopy::run( options, in, out );
  close( in );
  close( out );

  /* whatever it was told, what it was given to read is still there: it did not climb out of dest */
  struct stat st;
  if ( stat( said.c_str(), &st ) < 0 || st.st_size != static_cast<off_t>( size - 1 ) ) {
    abort();
  }
  empty( scratch + "/dest", false );
  return 0;
}
