/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/crypto/prng.h"
#include "src/meetup/master.h"

static const size_t MASTER_LEN = 16;

std::string Meetup::gosh_directory( std::string& error )
{
  const char* home = getenv( "HOME" );
#ifdef __APPLE__
  if ( home && *home ) {
    return std::string( home ) + "/Library/Application Support/gosh";
  }
#else
  const char* xdg = getenv( "XDG_CONFIG_HOME" );
  if ( xdg && *xdg ) {
    return std::string( xdg ) + "/gosh";
  }
  if ( home && *home ) {
    return std::string( home ) + "/.config/gosh";
  }
#endif
  error = "neither HOME nor XDG_CONFIG_HOME is set, so there is nowhere to keep the meetup's key";
  return std::string();
}

/* Makes the directory (and its parent) if it is not there; only its owner may use what it makes. */
static bool make_directory( const std::string& dir, std::string& error )
{
  const std::string parent = dir.substr( 0, dir.rfind( '/' ) );
  if ( !parent.empty() ) {
    mkdir( parent.c_str(), 0700 );
  }
  if ( mkdir( dir.c_str(), 0700 ) != 0 && errno != EEXIST ) {
    error = dir + ": " + strerror( errno );
    return false;
  }
  return true;
}

/* Writes a new key to a file beside `path`, then puts it in place. */
static bool write_master( const std::string& path, std::string& error )
{
  char key[MASTER_LEN];
  PRNG().fill( key, sizeof( key ) );
  const std::string temporary = path + ".new";
  unlink( temporary.c_str() );
  const int fd = open( temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600 );
  if ( fd < 0 ) {
    error = temporary + ": " + strerror( errno );
    return false;
  }
  const bool written = write( fd, key, sizeof( key ) ) == static_cast<ssize_t>( sizeof( key ) ) && fsync( fd ) == 0;
  close( fd );
  memset( key, 0, sizeof( key ) );
  if ( !written || rename( temporary.c_str(), path.c_str() ) != 0 ) {
    error = path + ": " + strerror( errno );
    unlink( temporary.c_str() );
    return false;
  }
  return true;
}

static std::string master_path( std::string& error )
{
  const std::string dir = Meetup::gosh_directory( error );
  if ( dir.empty() || !make_directory( dir, error ) ) {
    return std::string();
  }
  return dir + "/meetup.key";
}

bool Meetup::meetup_master( std::string& master, std::string& error )
{
  const std::string path = master_path( error );
  if ( path.empty() ) {
    return false;
  }
  int fd = open( path.c_str(), O_RDONLY | O_NOFOLLOW );
  if ( fd < 0 && errno == ENOENT ) {
    if ( !write_master( path, error ) ) {
      return false;
    }
    fd = open( path.c_str(), O_RDONLY | O_NOFOLLOW );
  }
  if ( fd < 0 ) {
    error = path + ": " + strerror( errno );
    return false;
  }
  struct stat st;
  char key[MASTER_LEN + 1];
  const bool stated = fstat( fd, &st ) == 0;
  const ssize_t n = read( fd, key, sizeof( key ) );
  close( fd );
  if ( !stated || ( st.st_mode & 077 ) != 0 || st.st_uid != geteuid() ) {
    error = path + ": others may read or change it; it must be its owner's alone (chmod 600)";
    return false;
  }
  if ( n != static_cast<ssize_t>( MASTER_LEN ) ) {
    error = path + ": not a key of 16 bytes (gosh-server meetup-key --new makes a new one)";
    return false;
  }
  master.assign( key, MASTER_LEN );
  memset( key, 0, sizeof( key ) );
  return true;
}

bool Meetup::meetup_master_new( std::string& error )
{
  const std::string path = master_path( error );
  return !path.empty() && write_master( path, error );
}
