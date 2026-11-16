/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-1 to REQ1/CP-5: the edge of copy (filecopy.cc), the fork's test of it: a sender and a
 * receiver, each a process, joined by a socket pair, copying real files in a directory of its own.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <csignal>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/copy/filecopy.h"
#include "src/copy/messages.h"

static int failures = 0;

#define CHECK( x )                                                                                                 \
  do {                                                                                                             \
    if ( !( x ) ) {                                                                                                \
      fprintf( stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x );                                      \
      failures++;                                                                                                  \
    }                                                                                                              \
  } while ( 0 )

static std::string root;

static std::string varied( size_t n, unsigned int seed )
{
  std::string s( n, '\0' );
  unsigned int x = seed * 2654435761u + 1;
  for ( size_t i = 0; i < n; i++ ) {
    x = x * 1664525u + 1013904223u;
    s[i] = static_cast<char>( x >> 24 );
  }
  return s;
}

static void put( const std::string& path, const std::string& content )
{
  std::ofstream out( ( root + "/" + path ).c_str(), std::ios::binary | std::ios::trunc );
  out << content;
}

static std::string get( const std::string& path )
{
  std::ifstream in( ( root + "/" + path ).c_str(), std::ios::binary );
  if ( !in ) {
    return "(missing)";
  }
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

static bool exists( const std::string& path )
{
  struct stat st;
  return stat( ( root + "/" + path ).c_str(), &st ) == 0;
}

static void dir( const std::string& path )
{
  mkdir( ( root + "/" + path ).c_str(), 0777 );
}

static std::vector<std::string> words( const std::string& line )
{
  std::vector<std::string> ret;
  std::istringstream in( line );
  std::string w;
  while ( in >> w ) {
    if ( w[0] != '-' && w != "send" && w != "receive" ) {
      w = root + "/" + w;
    }
    ret.push_back( w );
  }
  return ret;
}

struct Running
{
  pid_t sender, receiver;
};

/* Starts the two ends, each a process.  The receiver is the "local" one, and quiet. */
static Running start( const std::string& send, const std::string& receive )
{
  int pair[2];
  if ( socketpair( AF_UNIX, SOCK_STREAM, 0, pair ) < 0 ) {
    perror( "socketpair" );
    exit( 1 );
  }
  Running r;
  for ( int i = 0; i < 2; i++ ) {
    const pid_t pid = fork();
    if ( pid == 0 ) {
      close( pair[1 - i] );
      FileCopy::Options options;
      if ( !FileCopy::parse( words( i == 0 ? send : receive ), options ) ) {
        _exit( 9 );
      }
      options.local = i == 1;
      options.quiet = true;
      _exit( FileCopy::run( options, pair[i], pair[i] ) );
    }
    ( i == 0 ? r.sender : r.receiver ) = pid;
  }
  close( pair[0] );
  close( pair[1] );
  return r;
}

static int finish( const Running& r )
{
  int status = 0, other = 0;
  waitpid( r.receiver, &status, 0 );
  waitpid( r.sender, &other, 0 );
  return WIFEXITED( status ) ? WEXITSTATUS( status ) : 100;
}

static int copy( const std::string& send, const std::string& receive )
{
  return finish( start( send, receive ) );
}

typedef std::vector<std::pair<uint8_t, std::string> > Said;

/*
 * Plays the other end of a copy, and lies (found untested 2026-11-16, by the review of copy): the
 * real end runs with these words; this end says HELLO as its part needs, then what is given. The
 * real end must hang up at once, its socket closed within a few seconds, and end with status 1,
 * not carry on as if nothing were wrong. Returns its status, or 100 if it did not hang up.
 */
static int lied_to( const std::string& real, bool real_receives, const Said& said )
{
  int pair[2];
  if ( socketpair( AF_UNIX, SOCK_STREAM, 0, pair ) < 0 ) {
    perror( "socketpair" );
    exit( 1 );
  }
  const pid_t pid = fork();
  if ( pid == 0 ) {
    close( pair[1] );
    FileCopy::Options options;
    if ( !FileCopy::parse( words( real ), options ) ) {
      _exit( 9 );
    }
    options.local = real_receives;
    options.quiet = true;
    _exit( FileCopy::run( options, pair[0], pair[0] ) );
  }
  close( pair[0] );
  std::string out;
  FileCopy::frame( out, FileCopy::HELLO, std::string( FileCopy::MAGIC ) );
  if ( real_receives ) {
    FileCopy::frame( out, FileCopy::HELLO, FileCopy::flag_body( false ) );
  }
  for ( size_t i = 0; i < said.size(); i++ ) {
    FileCopy::frame( out, said[i].first, said[i].second );
  }
  if ( write( pair[1], out.data(), out.size() ) != static_cast<ssize_t>( out.size() ) ) {
    perror( "write" );
  }
  bool hung_up = false;
  for ( int waited = 0; waited < 50 && !hung_up; waited++ ) {
    struct pollfd p = { pair[1], POLLIN, 0 };
    if ( poll( &p, 1, 100 ) > 0 ) {
      char buf[65536];
      hung_up = read( pair[1], buf, sizeof( buf ) ) <= 0;
    }
  }
  close( pair[1] );
  if ( !hung_up ) {
    kill( pid, SIGKILL );
  }
  int status = 0;
  waitpid( pid, &status, 0 );
  return hung_up && WIFEXITED( status ) ? WEXITSTATUS( status ) : 100;
}

static std::string listed_file( const std::string& path, uint64_t size )
{
  FileCopy::Entry e;
  e.kind = 'F';
  e.path = path;
  e.size = size;
  e.mode = 0644;
  std::string body;
  FileCopy::put_listed( body, e );
  return body;
}

static bool eventually( bool ( *what )( void ) )
{
  for ( int i = 0; i < 100; i++ ) {
    if ( what() ) {
      return true;
    }
    usleep( 50000 );
  }
  return false;
}

static const std::string big = varied( 300000, 1 );

static bool auto_first( void )
{
  return get( "mirror/a.txt" ) == "one" && get( "mirror/sub/b.bin" ) == big;
}
static bool auto_changed( void )
{
  return get( "mirror/a.txt" ) == "two!" && get( "mirror/new.txt" ) == "new";
}
static bool auto_removed( void )
{
  return !exists( "mirror/sub/b.bin" ) && !exists( "mirror/gone" ) && get( "mirror/a.txt" ) == "two!";
}

int main( void )
{
  signal( SIGALRM, SIG_DFL );
  alarm( 120 );

  char scratch[] = "/tmp/mosh-file-copy-XXXXXX";
  if ( !mkdtemp( scratch ) ) {
    perror( "mkdtemp" );
    return 1;
  }
  root = scratch;

  /* (names and the checksum have their own tests: names-test.cc, checksum-test.cc) */

  /* one file, to a new name; then into a directory; then nothing to do */
  dir( "src" );
  dir( "out" );
  put( "src/a.txt", "one" );
  put( "src/empty", "" );
  CHECK( copy( "send -- src/a.txt", "receive -- out/renamed.txt" ) == 0 );
  CHECK( get( "out/renamed.txt" ) == "one" );
  CHECK( copy( "send -- src/a.txt src/empty", "receive -- out" ) == 0 );
  CHECK( get( "out/a.txt" ) == "one" && get( "out/empty" ) == "" );
  CHECK( copy( "send -- src/a.txt", "receive -- out/newdir/" ) == 0 );
  CHECK( get( "out/newdir/a.txt" ) == "one" );

  /* something that is not there is an error, and the rest is still copied */
  CHECK( copy( "send -- src/nothing src/a.txt", "receive -- out/newdir2/" ) == 1 );
  CHECK( get( "out/newdir2/a.txt" ) == "one" );
  /* a directory without -r */
  CHECK( copy( "send -- src", "receive -- out/not" ) == 1 );
  CHECK( !exists( "out/not" ) );

  /* a tree: under a new name, and into a directory that is there */
  dir( "src/sub" );
  dir( "src/sub/deep" );
  dir( "src/hollow" );
  put( "src/sub/b.bin", big );
  put( "src/sub/deep/c", "sea" );
  chmod( ( root + "/src/sub/deep/c" ).c_str(), 0755 );
  CHECK( copy( "send -r -- src", "receive -- tree" ) == 0 );
  CHECK( get( "tree/a.txt" ) == "one" && get( "tree/sub/b.bin" ) == big && get( "tree/sub/deep/c" ) == "sea" );
  CHECK( exists( "tree/hollow" ) );
  {
    struct stat st, was;
#ifndef __CYGWIN__ /* (Windows keeps no such thing: there, what can be run is decided by its name) */
    CHECK( stat( ( root + "/tree/sub/deep/c" ).c_str(), &st ) == 0 && ( st.st_mode & 0100 ) );
#endif
    CHECK( stat( ( root + "/src/sub/b.bin" ).c_str(), &was ) == 0
           && stat( ( root + "/tree/sub/b.bin" ).c_str(), &st ) == 0 && st.st_mtime == was.st_mtime );
  }
  CHECK( copy( "send -r -- src", "receive -- out" ) == 0 );
  CHECK( get( "out/src/sub/deep/c" ) == "sea" );
  /* "what is in this directory" */
  CHECK( copy( "send -r -- src/sub/.", "receive -- contents" ) == 0 );
  CHECK( get( "contents/b.bin" ) == big && get( "contents/deep/c" ) == "sea" );
  /* patterns */
  CHECK( copy( "send --glob -- src/sub/*.bin src/*.txt", "receive -- globbed" ) == 0 );
  CHECK( get( "globbed/b.bin" ) == big && get( "globbed/a.txt" ) == "one" && !exists( "globbed/empty" ) );

  /* (again into a directory that is there: so, its contents)
     a copy that was cut off is carried on from where it got to... */
  put( "tree/sub/.b.bin.mocp-part", big.substr( 0, 123456 ) );
  unlink( ( root + "/tree/sub/b.bin" ).c_str() );
  CHECK( copy( "send -r -- src/.", "receive -- tree" ) == 0 );
  CHECK( get( "tree/sub/b.bin" ) == big && !exists( "tree/sub/.b.bin.mocp-part" ) );
  /* ...unless what is there is not the start of this file */
  put( "tree/sub/.b.bin.mocp-part", varied( 5000, 7 ) );
  unlink( ( root + "/tree/sub/b.bin" ).c_str() );
  CHECK( copy( "send -r -- src/.", "receive -- tree" ) == 0 );
  CHECK( get( "tree/sub/b.bin" ) == big );

  /* a file here that differs is replaced; what is not at the source stays, unless asked */
  put( "tree/a.txt", "something else entirely" );
  put( "tree/sub/extra", "extra" );
  CHECK( copy( "send -r -- src/.", "receive -- tree" ) == 0 );
  CHECK( get( "tree/a.txt" ) == "one" && get( "tree/sub/extra" ) == "extra" );
  CHECK( copy( "send -r --delete -- src/.", "receive --delete -- tree" ) == 0 );
  CHECK( !exists( "tree/sub/extra" ) && get( "tree/sub/deep/c" ) == "sea" && get( "tree/a.txt" ) == "one" );

  /* a file where a directory should be, and the other way about */
  dir( "clash" );
  dir( "clash/a.txt" );
  CHECK( copy( "send -- src/a.txt", "receive -- clash" ) == 1 );

  /* (found 2026-11-16 by the review of copy, each seen red first)
     a file at the source with the name of another's part file is refused, alone, and the other is
     copied whole, whichever comes first (REQ1/CP-1, REQ1/CP-3) */
  dir( "pn" );
  put( "pn/x", big );
  put( "pn/.x.mocp-part", "a file of its own" );
  CHECK( copy( "send -r -- pn/.", "receive -- pnout" ) == 1 );
  CHECK( get( "pnout/x" ) == big && !exists( "pnout/.x.mocp-part" ) );

  /* the start of a file held here is kept while the source cannot be read for a while, and the copy
     carries on from it once it can (REQ1/CP-2) */
  if ( geteuid() != 0 ) { /* (root reads what is closed to everyone) */
    dir( "ur" );
    dir( "urout" );
    put( "ur/f", big );
    put( "urout/.f.mocp-part", big.substr( 0, 200000 ) );
    chmod( ( root + "/ur/f" ).c_str(), 0 );
    CHECK( copy( "send -- ur/f", "receive -- urout/" ) == 1 );
    CHECK( get( "urout/.f.mocp-part" ) == big.substr( 0, 200000 ) );
    chmod( ( root + "/ur/f" ).c_str(), 0644 );
    CHECK( copy( "send -- ur/f", "receive -- urout/" ) == 0 );
    CHECK( get( "urout/f" ) == big && !exists( "urout/.f.mocp-part" ) );

    /* a part that cannot be made here fails that file alone, and the rest is copied */
    dir( "shut" );
    put( "ur/g", "gee" );
    chmod( ( root + "/shut" ).c_str(), 0555 );
    CHECK( copy( "send -- ur/f ur/g", "receive -- shut" ) == 1 );
    chmod( ( root + "/shut" ).c_str(), 0755 );
    CHECK( !exists( "shut/f" ) && !exists( "shut/g" ) );
    CHECK( copy( "send -- ur/f ur/g", "receive -- shut" ) == 0 );
    CHECK( get( "shut/g" ) == "gee" );
  }

  /* an end that lies about where the copy is: the real one hangs up at once, with status 1 */
  {
    signal( SIGPIPE, SIG_IGN );
    dir( "lie" );
    put( "lie/f", "fff" );
    const std::string begun = FileCopy::begin_body( 0, 0 );
    const std::string wanted = FileCopy::want_body( FileCopy::Want() );
    Said s;
    s.push_back( std::make_pair( FileCopy::DATA, std::string( "x" ) ) );
    CHECK( lied_to( "receive -- lie/a", true, s ) == 1 ); /* DATA with no file begun */
    s.clear();
    s.push_back( std::make_pair( FileCopy::FILE_END, FileCopy::end_body( true, FileCopy::Hash() ) ) );
    CHECK( lied_to( "receive -- lie/b", true, s ) == 1 ); /* FILE_END with no file begun */
    s.clear();
    s.push_back( std::make_pair( FileCopy::LIST, listed_file( "f", 5 ) ) );
    s.push_back( std::make_pair( FileCopy::LIST_END, FileCopy::flag_body( true ) ) );
    s.push_back( std::make_pair( FileCopy::FILE_BEGIN, begun ) );
    s.push_back( std::make_pair( FileCopy::ROUND_END, std::string() ) );
    CHECK( lied_to( "receive -- lie/c", true, s ) == 1 ); /* ROUND_END with a file still arriving */
    s.clear();
    s.push_back( std::make_pair( FileCopy::LIST, listed_file( "f", 5 ) ) );
    s.push_back( std::make_pair( FileCopy::LIST_END, FileCopy::flag_body( true ) ) );
    s.push_back( std::make_pair( FileCopy::LIST_END, FileCopy::flag_body( true ) ) );
    CHECK( lied_to( "receive -- lie/d", true, s ) == 1 ); /* a second LIST_END in one round */
    s.clear();
    s.push_back( std::make_pair( FileCopy::LIST, listed_file( "f", 5 ) ) );
    s.push_back( std::make_pair( FileCopy::LIST_END, FileCopy::flag_body( true ) ) );
    s.push_back( std::make_pair( FileCopy::LIST, listed_file( "g", 5 ) ) );
    CHECK( lied_to( "receive -- lie/e", true, s ) == 1 ); /* a LIST after the plan is made */
    s.clear();
    s.push_back( std::make_pair( FileCopy::RESULT, FileCopy::result_body( 99, true, "" ) ) );
    CHECK( lied_to( "send -- lie/f", false, s ) == 1 ); /* a RESULT for something never listed */
    s.clear();
    s.push_back( std::make_pair( FileCopy::WANT, wanted ) );
    s.push_back( std::make_pair( FileCopy::WANT, wanted ) );
    CHECK( lied_to( "send -- lie/f", false, s ) == 1 ); /* the same file wanted twice */
  }

  /* kept up to date */
  {
    dir( "src/gone" );
    put( "src/gone/x", "x" );
    const Running r = start( "send -r --auto --delete --interval=100 -- src/.",
                             "receive --auto --delete -- mirror" );
    CHECK( eventually( auto_first ) );
    CHECK( get( "mirror/gone/x" ) == "x" );
    put( "src/a.txt", "two!" );
    put( "src/new.txt", "new" );
    CHECK( eventually( auto_changed ) );
    unlink( ( root + "/src/sub/b.bin" ).c_str() );
    unlink( ( root + "/src/gone/x" ).c_str() );
    rmdir( ( root + "/src/gone" ).c_str() );
    CHECK( eventually( auto_removed ) );
    CHECK( get( "mirror/sub/deep/c" ) == "sea" );
    kill( r.sender, SIGTERM );
    CHECK( finish( r ) == 1 ); /* the receiver is told the connection closed */
  }

  const std::string cleanup = "rm -rf '" + root + "'";
  if ( system( cleanup.c_str() ) != 0 ) {
    failures++;
  }
  if ( failures ) {
    fprintf( stderr, "file-copy: %d check(s) FAILED\n", failures );
    return 1;
  }
  printf( "file-copy: all checks passed\n" );
  return 0;
}
