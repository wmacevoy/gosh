/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * copy, the edge: the two blocking procedures, sender and receiver (design v2, 2.2: "keep the two
 * blocking procedures"). They read and write the stream, the file system, the clock and the user;
 * what they decide they ask of decisions.h, and the bytes they send are messages.h's. The fork's
 * (mosh-1.4.0-pf.7, src/network/filecopy.cc), with those moved out, HOME read once (in run) and
 * the clock passed in.
 */

#include "src/include/config.h"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/copy/imp/decisions.h"
#include "src/copy/filecopy.h"
#include "src/copy/imp/messages.h"

using namespace Gosh::Copy;
using namespace Gosh::Copy::Imp;

namespace {

const size_t OUT_HIGH = 256 * 1024; /* how much may wait to be written before we wait for it */

/* The clock: milliseconds, never going back. */
typedef uint64_t ( *Clock )( void );

/* The other end has gone. */
struct Closed : public std::runtime_error
{
  Closed() : std::runtime_error( "the connection was closed" ) {}
};

uint64_t now_ms( void )
{
  struct timespec t;
#ifdef CLOCK_MONOTONIC
  clock_gettime( CLOCK_MONOTONIC, &t );
#else
  clock_gettime( CLOCK_REALTIME, &t );
#endif
  return static_cast<uint64_t>( t.tv_sec ) * 1000 + t.tv_nsec / 1000000;
}

/*
 * Messages both ways over a pair of descriptors.  Nothing here waits to
 * write without also reading, so the two ends cannot each be stuck
 * waiting for the other to take what it has sent.
 */
class Channel
{
private:
  int in, out;
  Clock now;
  std::string inbox, outbox;

  /* Waits (up to timeout, ms; -1 for as long as it takes) for something to do, and does it. */
  void turn( int timeout )
  {
    struct pollfd fds[2] = { { in, POLLIN, 0 }, { outbox.empty() ? -1 : out, POLLOUT, 0 } };
    if ( poll( fds, 2, timeout ) < 0 ) {
      if ( errno == EINTR ) {
        return;
      }
      throw Closed();
    }
    if ( fds[1].revents ) {
      ssize_t n = write( out, outbox.data(), outbox.size() );
      if ( n > 0 ) {
        outbox.erase( 0, n );
      } else if ( n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR ) {
        throw Closed();
      }
    }
    if ( fds[0].revents ) {
      char buf[65536];
      ssize_t n = read( in, buf, sizeof( buf ) );
      if ( n > 0 ) {
        inbox.append( buf, n );
      } else if ( n == 0 || ( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR ) ) {
        throw Closed();
      }
    }
  }

public:
  Channel( int s_in, int s_out, Clock s_now ) : in( s_in ), out( s_out ), now( s_now ), inbox(), outbox()
  {
    fcntl( in, F_SETFL, fcntl( in, F_GETFL ) | O_NONBLOCK );
    fcntl( out, F_SETFL, fcntl( out, F_GETFL ) | O_NONBLOCK );
  }

  void send( uint8_t type, const std::string& payload )
  {
    frame( outbox, type, payload );
    while ( outbox.size() > OUT_HIGH ) {
      turn( -1 );
    }
  }

  /* False if no message came within timeout (ms; -1 to wait for one, 0 not to wait). */
  bool recv( uint8_t& type, std::string& payload, int timeout )
  {
    const uint64_t deadline = now() + ( timeout > 0 ? timeout : 0 );
    while ( true ) {
      if ( unframe( inbox, type, payload ) ) {
        return true;
      }
      if ( timeout < 0 ) {
        turn( -1 );
      } else {
        const uint64_t at = now();
        if ( timeout == 0 ) {
          /* whatever is there already, and no more */
          struct pollfd fd = { in, POLLIN, 0 };
          if ( poll( &fd, 1, 0 ) <= 0 ) {
            return false;
          }
          turn( 0 );
          timeout = 1; /* (and then look at what came, once) */
          continue;
        }
        if ( at >= deadline ) {
          return false;
        }
        turn( static_cast<int>( deadline - at ) );
      }
    }
  }

  void flush( void )
  {
    while ( !outbox.empty() ) {
      turn( -1 );
    }
  }
};

long nanoseconds( const struct stat& st )
{
#if defined( __APPLE__ )
  return st.st_mtimespec.tv_nsec;
#elif defined( __linux__ ) || defined( __CYGWIN__ ) || defined( __FreeBSD__ ) || defined( __OpenBSD__ )            \
  || defined( __NetBSD__ )
  return st.st_mtim.tv_nsec;
#else
  (void)st;
  return 0;
#endif
}

/* What stat says, as decisions.h takes it. */
Seen seen( int ok, const struct stat& st )
{
  Seen s;
  s.exists = ok == 0;
  if ( s.exists ) {
    s.directory = S_ISDIR( st.st_mode );
    s.regular = S_ISREG( st.st_mode );
    s.size = st.st_size;
    s.mtime = st.st_mtime;
    s.nsec = nanoseconds( st );
  }
  return s;
}

Seen seen_at( const std::string& path )
{
  struct stat st;
  const int ok = stat( path.c_str(), &st );
  return seen( ok, st );
}

std::string human( uint64_t bytes )
{
  char buf[32];
  if ( bytes < 10000 ) {
    snprintf( buf, sizeof( buf ), "%u bytes", static_cast<unsigned int>( bytes ) );
  } else if ( bytes < 10000ULL * 1024 ) {
    snprintf( buf, sizeof( buf ), "%.1f KB", bytes / 1024.0 );
  } else if ( bytes < 10000ULL * 1024 * 1024 ) {
    snprintf( buf, sizeof( buf ), "%.1f MB", bytes / 1048576.0 );
  } else {
    snprintf( buf, sizeof( buf ), "%.1f GB", bytes / 1073741824.0 );
  }
  return buf;
}

bool make_directories( const std::string& path )
{
  struct stat st;
  if ( stat( path.c_str(), &st ) == 0 ) {
    if ( S_ISDIR( st.st_mode ) ) {
      return true;
    }
    errno = ENOTDIR;
    return false;
  }
  const size_t slash = path.rfind( '/' );
  if ( slash != std::string::npos && slash > 0 && !make_directories( path.substr( 0, slash ) ) ) {
    return false;
  }
  return mkdir( path.c_str(), 0777 ) == 0 || errno == EEXIST;
}

void remove_tree( const std::string& path )
{
  struct stat st;
  if ( lstat( path.c_str(), &st ) < 0 ) {
    return;
  }
  if ( S_ISDIR( st.st_mode ) ) {
    std::vector<std::string> names;
    if ( DIR* dir = opendir( path.c_str() ) ) {
      while ( struct dirent* d = readdir( dir ) ) {
        const std::string name = d->d_name;
        if ( name != "." && name != ".." ) {
          names.push_back( name );
        }
      }
      closedir( dir );
    }
    for ( size_t i = 0; i < names.size(); i++ ) {
      remove_tree( child_of( path, names[i] ) );
    }
    rmdir( path.c_str() );
  } else {
    unlink( path.c_str() );
  }
}

/* What the two ends have in common. */
class End
{
protected:
  const Options& options;
  const char* home; /* HOME, read once, for "~" in the paths */
  Clock now;
  Channel channel;
  unsigned int errors; /* things that went wrong, at either end */
  unsigned int round;
  bool progress_shown;
  uint64_t progress_at;

  /* unused */
  End( const End& );
  End& operator=( const End& );

  void clear_progress( void )
  {
    if ( progress_shown ) {
      fputs( "\r\033[K", stderr );
      progress_shown = false;
    }
  }

  void print( const std::string& line )
  {
    clear_progress();
    fprintf( stderr, "%s\n", line.c_str() );
    fflush( stderr );
  }

  /* Something the user should hear.  It is said here if they are here, and sent to them if not. */
  void say( bool error, const std::string& text )
  {
    if ( error ) {
      errors++;
    }
    if ( options.local ) {
      if ( error || !options.quiet ) {
        print( "gocp: " + text );
      }
    } else {
      channel.send( NOTE, note_body( error, text ) );
    }
  }

  void heard( const std::string& payload )
  {
    bool error;
    std::string text;
    read_note( payload, error, text );
    if ( error ) {
      errors++;
    }
    if ( options.local && ( error || !options.quiet ) ) {
      print( "gocp: " + text );
    }
  }

  /* A file has arrived, or gone: said at the user's end. */
  void report( const char* what, std::string path, uint64_t size )
  {
    if ( !options.local || options.quiet ) {
      return;
    }
    std::string line;
    if ( options.automatic && round > 0 ) {
      char stamp[16];
      const time_t t = time( NULL );
      struct tm tm;
      localtime_r( &t, &tm );
      strftime( stamp, sizeof( stamp ), "%H:%M:%S ", &tm );
      line = stamp;
    }
    line += what;
    line += path.compare( 0, 2, "./" ) == 0 ? path.substr( 2 ) : path;
    if ( what[0] != 'r' ) {
      line += "  (" + human( size ) + ")";
    }
    print( line );
  }

  /* How far along a large file is, if someone is watching. */
  void progress( std::string path, uint64_t done, uint64_t total )
  {
    if ( !options.local || options.quiet || total < 4 * CHUNK || !isatty( STDERR_FILENO ) ) {
      return;
    }
    const uint64_t at = now();
    if ( at - progress_at < 250 ) {
      return;
    }
    progress_at = at;
    const std::string name = path.size() > 50 ? "..." + path.substr( path.size() - 47 ) : path;
    fprintf( stderr, "\r\033[K%s  %3u%% of %s", name.c_str(), static_cast<unsigned int>( done * 100 / total ),
             human( total ).c_str() );
    fflush( stderr );
    progress_shown = true;
  }

  void hello( void )
  {
    std::string m( MAGIC );
    channel.send( HELLO, m );
    uint8_t type;
    std::string payload;
    channel.recv( type, payload, -1 );
    if ( type != HELLO || payload != MAGIC ) {
      throw Malformed();
    }
  }

public:
  End( const Options& s_options, const char* s_home, Clock s_now, int in, int out )
    : options( s_options ), home( s_home ), now( s_now ), channel( in, out, s_now ), errors( 0 ), round( 0 ),
      progress_shown( false ), progress_at( 0 )
  {}
  virtual ~End() {}
  virtual void run( void ) = 0;

  int status( void )
  {
    clear_progress();
    return errors ? 1 : 0;
  }

  void failed( const std::string& why )
  {
    errors++;
    if ( options.local ) {
      print( "gocp: " + why );
    }
  }
};

class Sender : public End
{
private:
  std::map<std::string, Entry> known; /* what the other end has been told of, as it then was */
  std::vector<Entry> entries;         /* this round's */
  std::vector<int> state;             /* of each: see below */
  bool plan_complete, round_complete;
  size_t copied, current;

  enum
  {
    UP_TO_DATE,
    WANTED,
    SENT_OK,
    SENT_BAD
  };

  std::vector<Want> wants;

  void look_at( const std::string& source, const std::string& path, bool top, std::map<std::string, Entry>& found )
  {
    const bool complain = round == 0;
    struct stat st;
    if ( stat( source.c_str(), &st ) < 0 ) {
      if ( complain && top ) {
        say( true, source + ": " + strerror( errno ) );
      }
      return; /* (inside a directory, it has just gone: nothing to remark on) */
    }
    Entry e;
    e.path = path;
    e.source = source;
    e.mtime = st.st_mtime;
    e.nsec = nanoseconds( st );
    e.mode = st.st_mode & 0777;
    if ( S_ISREG( st.st_mode ) ) {
      e.kind = 'F';
      e.size = st.st_size;
      found[path] = e;
    } else if ( S_ISDIR( st.st_mode ) ) {
      if ( !options.recursive ) {
        if ( complain ) {
          say( true, source + " is a directory (not copied: use -r)" );
        }
        return;
      }
      e.kind = 'D';
      e.mtime = 0; /* a directory is the same directory whenever it was last touched */
      e.nsec = 0;
      found[path] = e;
      std::vector<std::string> names;
      DIR* dir = opendir( source.c_str() );
      if ( !dir ) {
        if ( complain ) {
          say( true, source + ": " + strerror( errno ) );
        }
        return;
      }
      while ( struct dirent* d = readdir( dir ) ) {
        const std::string name = d->d_name;
        if ( name != "." && name != ".." ) {
          names.push_back( name );
        }
      }
      closedir( dir );
      for ( size_t i = 0; i < names.size(); i++ ) {
        const std::string child = child_of( source, names[i] );
        struct stat link;
        if ( lstat( child.c_str(), &link ) == 0 && S_ISLNK( link.st_mode ) && stat( child.c_str(), &st ) == 0
             && S_ISDIR( st.st_mode ) ) {
          if ( complain ) {
            say( false, child + " is a link to a directory (not followed)" );
          }
          continue;
        }
        look_at( child, path + "/" + names[i], false, found );
      }
    } else if ( complain ) {
      say( false, source + " is not a file or a directory (not copied)" );
    }
  }

  /* The sources as given, with patterns expanded if they are patterns. */
  std::vector<std::string> sources( void )
  {
    std::vector<std::string> ret;
    for ( size_t i = 0; i < options.paths.size(); i++ ) {
      const std::string path = expand_home( options.paths[i], home );
      if ( options.glob ) {
        glob_t matches;
        memset( &matches, 0, sizeof( matches ) );
        if ( glob( path.c_str(), GLOB_NOCHECK, NULL, &matches ) == 0 ) {
          for ( size_t j = 0; j < matches.gl_pathc; j++ ) {
            ret.push_back( matches.gl_pathv[j] );
          }
        } else {
          ret.push_back( path );
        }
        globfree( &matches );
      } else {
        ret.push_back( path );
      }
    }
    return ret;
  }

  void scan( void )
  {
    std::map<std::string, Entry> found;
    const std::vector<std::string> from = sources();
    for ( size_t i = 0; i < from.size(); i++ ) {
      look_at( from[i], top_name( from[i] ), true, found );
    }
    entries = changes( found, known, round, options.remove );
  }

  void incoming( uint8_t type, const std::string& payload )
  {
    switch ( type ) {
      case WANT: {
        const Want w = read_want( payload );
        if ( !want_acceptable( w, entries, w.index < state.size() && state[w.index] != UP_TO_DATE ) ) {
          throw Malformed();
        }
        state[w.index] = WANTED;
        wants.push_back( w );
        break;
      }
      case PLAN_END:
        plan_complete = true;
        break;
      case RESULT: {
        uint32_t index;
        bool ok;
        std::string why;
        read_result( payload, index, ok, why );
        if ( index >= entries.size() ) {
          throw Malformed();
        }
        state[index] = ok ? SENT_OK : SENT_BAD;
        if ( ok ) {
          copied++;
          report( "", entries[index].path, entries[index].size );
        } else {
          errors++;
          if ( options.local && !why.empty() ) {
            print( "gocp: " + entries[index].path + ": " + why );
          }
        }
        break;
      }
      case ROUND_DONE:
        round_complete = true;
        break;
      case NOTE:
        heard( payload );
        break;
      default:
        throw Malformed();
    }
  }

  void drain( void )
  {
    uint8_t type;
    std::string payload;
    while ( channel.recv( type, payload, 0 ) ) {
      incoming( type, payload );
    }
  }

  void send_file( Want want )
  {
    const Entry& e = entries[want.index];
    bool ok = true;
    uint64_t offset = want.offset;
    Hash hash;
    char buf[CHUNK];

    const int fd = open( e.source.c_str(), O_RDONLY );
    if ( fd < 0 ) {
      say( true, e.source + ": " + strerror( errno ) );
      errors--; /* (it will be counted when the other end says it did not get it) */
      ok = false;
      /* (gosh) offset stays where they asked: from 0 they would throw away the start they hold, which
         this file's being unreadable for a while says nothing against (REQ1/CP-2; found 2026-11-16) */
    }

    /* If they hold the start of it already, is it the start of this? */
    if ( ok && offset > 0 ) {
      uint64_t done = 0;
      while ( done < offset && offset <= e.size ) {
        ssize_t n = read( fd, buf, std::min<uint64_t>( sizeof( buf ), offset - done ) );
        if ( n <= 0 ) {
          break;
        }
        hash.add( buf, n );
        done += n;
      }
      offset = send_from( offset, e.size, done, hash, want.prefix );
      if ( offset == 0 ) {
        hash = Hash();
        lseek( fd, 0, SEEK_SET );
      }
    }

    channel.send( FILE_BEGIN, begin_body( want.index, offset ) );

    uint64_t at = offset;
    while ( ok && at < e.size ) {
      ssize_t n = read( fd, buf, std::min<uint64_t>( sizeof( buf ), e.size - at ) );
      if ( n <= 0 ) {
        ok = false; /* shorter than it was */
        break;
      }
      hash.add( buf, n );
      at += n;
      channel.send( DATA, std::string( buf, n ) );
      progress( e.path, at, e.size );
      drain();
    }
    if ( fd >= 0 ) {
      struct stat st;
      const int got = fstat( fd, &st );
      if ( !unchanged_while_read( e, seen( got, st ) ) ) {
        ok = false; /* written to while we were reading it */
      }
      close( fd );
    }

    channel.send( FILE_END, end_body( ok, hash ) );
  }

  void one_round( void )
  {
    state.assign( entries.size(), UP_TO_DATE );
    wants.clear();
    plan_complete = round_complete = false;
    copied = 0;

    std::string m;
    for ( size_t i = 0; i < entries.size(); i++ ) {
      put_listed( m, entries[i] );
      if ( m.size() > LIST_BATCH ) {
        channel.send( LIST, m );
        m.clear();
      }
    }
    if ( !m.empty() ) {
      channel.send( LIST, m );
    }
    channel.send( LIST_END, flag_body( round == 0 ) ); /* the first is everything there is */

    uint8_t type;
    std::string payload;
    while ( !plan_complete ) {
      channel.recv( type, payload, -1 );
      incoming( type, payload );
    }
    for ( current = 0; current < wants.size(); current++ ) {
      send_file( wants[current] );
    }
    channel.send( ROUND_END, "" );
    while ( !round_complete ) {
      channel.recv( type, payload, -1 );
      incoming( type, payload );
    }
    clear_progress();

    /* Whatever became of each, it is not offered again until it changes. */
    for ( size_t i = 0; i < entries.size(); i++ ) {
      if ( entries[i].kind == 'X' ) {
        report( "removed ", entries[i].path, 0 );
      } else {
        known[entries[i].path] = entries[i];
      }
    }
  }

public:
  Sender( const Options& s_options, const char* s_home, Clock s_now, int in, int out )
    : End( s_options, s_home, s_now, in, out ), known(), entries(), state(), plan_complete( false ),
      round_complete( false ), copied( 0 ), current( 0 ), wants()
  {}

  void run( void )
  {
    channel.send( HELLO, std::string( MAGIC ) );
    /* (more than one thing to copy means the destination is a directory to put them in) */
    channel.send( HELLO, flag_body( sources().size() > 1 ) );
    uint8_t type;
    std::string payload;
    channel.recv( type, payload, -1 );
    if ( type != HELLO || payload != MAGIC ) {
      throw Malformed();
    }
    scan();

    while ( true ) {
      if ( round == 0 || !entries.empty() ) {
        one_round();
        if ( round == 0 && options.local && !options.quiet ) {
          if ( copied == 0 && errors == 0 ) {
            print( "gocp: everything is up to date already." );
          }
          if ( options.automatic ) {
            print( "gocp: watching for changes. Interrupt (Ctrl-C) to stop." );
          }
        }
        round++;
      }
      if ( !options.automatic ) {
        break;
      }
      /* wait a while, listening in case the other end has something to say, or goes */
      const uint64_t until = now() + options.interval;
      while ( now() < until ) {
        if ( channel.recv( type, payload, static_cast<int>( until - now() ) ) ) {
          incoming( type, payload );
        }
      }
      scan();
    }
    channel.send( BYE, "" );
    channel.flush();
  }
};

class Receiver : public End
{
private:
  std::string dest;
  bool many, into, into_known;

  std::vector<Plan> plans;
  size_t copied;

  /* whether the plan for this round is made: a LIST or LIST_END after it is not the protocol */
  bool planned;

  /* the file arriving now; arriving is what the other end's messages are checked against, and fd may
     be -1 while it is (its part could not be opened: it fails alone, as one that cannot be written) */
  bool arriving;
  int fd;
  uint32_t index;
  bool ok;
  std::string trouble;
  Hash hash;
  uint64_t at;

  void result( uint32_t which, bool good, std::string why )
  {
    if ( good ) {
      copied++;
      report( "", plans[which].entry.path, plans[which].entry.size );
    } else {
      errors++;
      if ( options.local && !why.empty() ) {
        print( "gocp: " + plans[which].target + ": " + why );
      }
    }
    channel.send( RESULT, result_body( which, good, why ) );
  }

  void plan_one( uint32_t which )
  {
    Plan& p = plans[which];
    const Entry& e = p.entry;

    if ( e.kind == 'X' ) {
      if ( may_remove( options.remove, p.target, dest ) ) {
        remove_tree( p.target );
        unlink( p.part.c_str() );
        report( "removed ", e.path, 0 );
      }
      return;
    }
    if ( e.kind == 'D' ) {
      if ( !make_directories( p.target ) ) {
        say( true, p.target + ": " + strerror( errno ) );
      }
      return;
    }

    /* (gosh) One with the name a part file has would be taken for another file's part (REQ1/CP-3) */
    if ( is_part_name( p.target ) ) {
      result( which, false, "its name is the one gocp gives a part file" );
      return;
    }
    const size_t slash = p.target.rfind( '/' );
    if ( slash != std::string::npos && slash > 0 && !make_directories( p.target.substr( 0, slash ) ) ) {
      result( which, false, strerror( errno ) );
      return;
    }
    switch ( holding( e, seen_at( p.target ) ) ) {
      case IN_THE_WAY:
        result( which, false, "is a directory here" );
        return;
      case HAVE_IT: {
        struct stat st;
        if ( stat( p.target.c_str(), &st ) == 0 && ( st.st_mode & 0777 ) != ( e.mode & ~process_umask() ) ) {
          chmod( p.target.c_str(), e.mode & ~process_umask() );
        }
        return; /* we have it */
      }
      case WANT_IT:
        break;
    }

    /* We need it.  Is there some of it here from last time? */
    p.wanted = true;
    if ( part_counts( e, seen_at( p.part ) ) ) {
      const int part = open( p.part.c_str(), O_RDONLY );
      if ( part >= 0 ) {
        char buf[CHUNK];
        ssize_t n;
        while ( ( n = read( part, buf, sizeof( buf ) ) ) > 0 ) {
          p.prefix.add( buf, n );
          p.offset += n;
        }
        close( part );
      }
    }
    Want w;
    w.index = which;
    w.offset = p.offset;
    w.prefix = p.prefix;
    channel.send( WANT, want_body( w ) );
  }

  static mode_t process_umask( void )
  {
    const mode_t mask = umask( 0 );
    umask( mask );
    return mask;
  }

  /* With everything listed, what is here and was not listed is to go. */
  void prune( void )
  {
    const std::set<std::string> listed = listed_here( plans );
    for ( size_t i = 0; i < plans.size(); i++ ) {
      if ( plans[i].entry.kind != 'D' ) {
        continue;
      }
      std::vector<std::string> names;
      if ( DIR* dir = opendir( plans[i].target.c_str() ) ) {
        while ( struct dirent* d = readdir( dir ) ) {
          const std::string name = d->d_name;
          if ( name != "." && name != ".." ) {
            names.push_back( name );
          }
        }
        closedir( dir );
      }
      for ( size_t j = 0; j < names.size(); j++ ) {
        const std::string path = child_of( plans[i].target, names[j] );
        if ( listed.find( path ) == listed.end() ) {
          remove_tree( path );
          report( "removed ", plans[i].entry.path + "/" + names[j], 0 );
        }
      }
    }
  }

  void plan( bool complete )
  {
    if ( !into_known ) {
      /* Is the destination a place to put things, or the name for the one thing? */
      const Seen d = seen_at( dest );
      into = destination_is_place( many, options.paths[0], d.exists && d.directory );
      into_known = true;
      if ( into && !make_directories( dest ) ) {
        say( true, dest + ": " + strerror( errno ) );
      }
    }
    for ( size_t i = 0; i < plans.size(); i++ ) {
      plans[i].target = target_of( dest, into, plans[i].entry.path );
      plans[i].part = part_name( plans[i].target );
    }
    for ( size_t i = 0; i < plans.size(); i++ ) {
      plan_one( i );
    }
    if ( complete && options.remove ) {
      prune();
    }
    channel.send( PLAN_END, "" );
  }

  void begin( const std::string& payload )
  {
    uint64_t offset;
    read_begin( payload, index, offset );
    if ( !begin_acceptable( arriving, index, offset, plans ) ) {
      throw Malformed();
    }
    const Plan& p = plans[index];
    hash = offset ? p.prefix : Hash();
    at = offset;
    ok = true;
    trouble.clear();
    fd = open( p.part.c_str(), O_WRONLY | O_CREAT, 0600 );
    if ( fd < 0 || ftruncate( fd, offset ) < 0 || lseek( fd, offset, SEEK_SET ) < 0 ) {
      ok = false;
      trouble = strerror( errno );
    }
    arriving = true;
  }

  void data( const std::string& payload )
  {
    if ( !arriving ) {
      throw Malformed();
    }
    hash.add( payload.data(), payload.size() );
    at += payload.size();
    size_t done = 0;
    while ( ok && done < payload.size() ) {
      ssize_t n = write( fd, payload.data() + done, payload.size() - done );
      if ( n < 0 ) {
        if ( errno == EINTR ) {
          continue;
        }
        ok = false;
        trouble = strerror( errno );
        break;
      }
      done += n;
    }
    progress( plans[index].entry.path, at, plans[index].entry.size );
  }

  void end( const std::string& payload )
  {
    bool sender_ok;
    Hash theirs;
    read_end( payload, sender_ok, theirs );
    if ( !arriving ) {
      throw Malformed();
    }
    arriving = false;
    const Plan& p = plans[index];
    /* (gosh) On the disk before it can take its name, so that a power cut leaves no file under its
       real name unless it is whole (requirements, Operating envelope, "Interruption"; REQ1/CP-3) */
    if ( fd >= 0 && ok && sender_ok && fsync( fd ) < 0 ) {
      ok = false;
      trouble = strerror( errno );
    }
    if ( fd >= 0 && close( fd ) < 0 && ok ) {
      ok = false;
      trouble = strerror( errno );
    }
    fd = -1;

    switch ( arrival( sender_ok, ok, hash, theirs, at, p.entry.size ) ) {
      case SENDER_FAILED:
        /* What came may yet be a start, unless nothing did. */
        if ( at == 0 ) {
          unlink( p.part.c_str() );
        }
        result( index, false, ok ? "it changed while it was being copied, or could not be read" : trouble );
        return;
      case NOT_AS_SENT:
        ok = false;
        trouble = "what arrived was not what was sent";
        unlink( p.part.c_str() );
        break;
      case NOT_WRITTEN:
        break;
      case WHOLE: {
        struct timeval times[2];
        times[0].tv_sec = times[1].tv_sec = p.entry.mtime;
        times[0].tv_usec = times[1].tv_usec = 0;
        chmod( p.part.c_str(), p.entry.mode & ~process_umask() );
        utimes( p.part.c_str(), times );
        if ( rename( p.part.c_str(), p.target.c_str() ) < 0 ) {
          ok = false;
          trouble = strerror( errno );
        }
        break;
      }
    }
    result( index, ok, trouble );
  }

public:
  Receiver( const Options& s_options, const char* s_home, Clock s_now, int in, int out )
    : End( s_options, s_home, s_now, in, out ),
      dest( without_trailing_slashes( expand_home( s_options.paths[0], s_home ) ) ), many( false ), into( false ),
      into_known( false ), plans(), copied( 0 ), planned( false ), arriving( false ), fd( -1 ), index( 0 ), ok( false ), trouble(), hash(), at( 0 )
  {}

  ~Receiver()
  {
    if ( fd >= 0 ) {
      close( fd );
    }
  }

  void run( void )
  {
    hello();
    uint8_t type;
    std::string payload;
    channel.recv( type, payload, -1 );
    if ( type != HELLO ) {
      throw Malformed();
    }
    many = read_flag( payload );

    while ( true ) {
      channel.recv( type, payload, -1 );
      switch ( type ) {
        case LIST: {
          if ( planned ) {
            throw Malformed();
          }
          const std::vector<Entry> listed = read_list( payload );
          for ( size_t i = 0; i < listed.size(); i++ ) {
            Plan p;
            p.entry = listed[i];
            plans.push_back( p );
          }
          break;
        }
        case LIST_END:
          if ( planned ) {
            throw Malformed();
          }
          planned = true;
          copied = 0;
          plan( read_flag( payload ) );
          break;
        case FILE_BEGIN:
          begin( payload );
          break;
        case DATA:
          data( payload );
          break;
        case FILE_END:
          end( payload );
          break;
        case ROUND_END:
          if ( arriving ) {
            throw Malformed();
          }
          planned = false;
          clear_progress();
          if ( round == 0 && options.local && !options.quiet ) {
            if ( copied == 0 && errors == 0 ) {
              print( "gocp: everything is up to date already." );
            }
            if ( options.automatic ) {
              print( "gocp: watching for changes. Interrupt (Ctrl-C) to stop." );
            }
          }
          round++;
          plans.clear();
          channel.send( ROUND_DONE, "" );
          break;
        case NOTE:
          heard( payload );
          break;
        case BYE:
          return;
        default:
          throw Malformed();
      }
    }
  }
};

}

int Gosh::Copy::run( const Options& options, int in_fd, int out_fd )
{
  signal( SIGPIPE, SIG_IGN );
  const char* home = getenv( "HOME" ); /* the one read of the environment: for "~" in the paths */
  End* end = NULL;
  int status = 1;
  try {
    if ( options.sender ) {
      end = new Sender( options, home, now_ms, in_fd, out_fd );
    } else {
      end = new Receiver( options, home, now_ms, in_fd, out_fd );
    }
    try {
      end->run();
    } catch ( const std::exception& e ) {
      end->failed( e.what() );
    }
    status = end->status();
  } catch ( const std::exception& e ) {
    if ( options.local ) {
      fprintf( stderr, "gocp: %s\n", e.what() );
    }
  }
  delete end;
  return status;
}
