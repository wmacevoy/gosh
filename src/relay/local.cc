/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * relay, the local relay: a process of its own on the user's machine, between the client (by
 * UDP on loopback) and the server (by UDP directly, and by an ssh connection it starts when
 * that is not answering) (REQ1/PATH-1, REQ1/PATH-2). An edge: sockets, the ssh child, the
 * clock, the environment in one place. What it decides it asks of paths.h, settings.h and
 * whose.h. The fork's text (mosh-1.4.0-pf.7, src/network/relay.cc, LocalRelay and
 * relay_start_local) but at the places marked (gosh).
 */

#include "src/relay/process.h"

namespace {

/* The local relay, once it is a process of its own. */
class LocalRelay
{
private:
  RelayOptions options;
  int client_fd; /* UDP, on loopback: the mosh client talks to this */
  int direct_fd; /* UDP, to the server */
  int alive_fd;  /* reads as ended when the client has gone */

  Addr client;
  socklen_t client_len;

  /* the remote relay, reached through a child process (ssh) */
  pid_t child;
  int to_child, from_child;
  std::string backlog;
  RelayFrameParser parser;
  /* (gosh) which way a datagram goes, and when the stream is started and let go: logic, in paths.h */
  Gosh::LocalPaths paths;

  /* a way straight through to the remote relay, when both ends are behind NATs (punch.h) */
  bool punching;
  std::vector<std::string> ask; /* meetups and STUN servers: whom to ask where we appear to be */
  std::string meetup_key; /* for a meetup, which answers only those who have it */
  std::unique_ptr<Punch> punch;
  bool was_open;

  /* (gosh) What the environment tells the relay: read here, and made sense of in settings.h. */
  static Gosh::RelaySettings told( bool stream_only )
  {
    return Gosh::relay_settings( stream_only, getenv( "MOSH_RELAY_STREAM_IDLE_MS" ), getenv( "MOSH_PUNCH" ),
                                 getenv( "MOSH_MEETUP" ), getenv( "MOSH_STUN" ), getenv( "MOSH_MEETUP_KEY" ) );
  }

  /*
   * (gosh) Whether a datagram on the loopback socket is from our client: from where the last
   * one came, or from a socket of this user's (whose.h). The kernel's list is read only when
   * the datagram is from somewhere new. Where there is no list to read, it cannot be told, and
   * the answer is yes, as it always was in the fork.
   */
  bool from_our_client( const Addr& from, socklen_t from_len )
  {
    if ( client_len && Gosh::same_udp_address( &from.sa, from_len, &client.sa, client_len ) ) {
      return true;
    }
    if ( from.sa.sa_family != AF_INET ) {
      return false; /* (the socket is an IPv4 one, on loopback) */
    }
    FILE* list = fopen( "/proc/net/udp", "r" );
    if ( !list ) {
      return true;
    }
    std::string sockets;
    char part[4096];
    size_t n;
    while ( ( n = fread( part, 1, sizeof( part ), list ) ) > 0 ) {
      sockets.append( part, n );
    }
    fclose( list );
    const bool ours = Gosh::udp_sender_is_users( sockets, from.sin.sin_addr.s_addr, ntohs( from.sin.sin_port ), getuid() );
    if ( !ours ) {
      say( "a datagram on the client's socket from port %d, which is not a socket of ours: not passed on",
           static_cast<int>( ntohs( from.sin.sin_port ) ) );
    }
    return ours;
  }

  void open_direct( void )
  {
    if ( direct_fd >= 0 ) {
      close( direct_fd );
    }
    direct_fd = options.stream_only ? -1 : socket( options.server.sa.sa_family, SOCK_DGRAM, 0 );
  }

  void stop_child( void )
  {
    if ( child > 0 ) {
      close( to_child );
      close( from_child );
      /* With its stream gone it should go of itself; then it is told to; then it is made to. */
      for ( int tries = 0; tries < 60; tries++ ) {
        if ( waitpid( child, NULL, WNOHANG ) != 0 ) {
          child = -1;
          break;
        }
        if ( tries == 20 ) {
          kill( child, SIGTERM );
        }
        usleep( 10000 );
      }
      if ( child > 0 ) {
        kill( child, SIGKILL );
        waitpid( child, NULL, 0 );
      }
      child = -1;
      to_child = from_child = -1;
      backlog.clear();
      parser = RelayFrameParser();
      paths.stream_stopped();
    }
  }

  /* (gosh) Whether and when is paths.h's to say; this starts it, and says how that went. */
  void start_child( void )
  {
    paths.stream_started( false );
    /* (What we had of a way through is no use, or we would not be here.  The new relay will have a new one.) */
    punch.reset();
    was_open = false;
    say( "starting the stream" );

    int in[2], out[2];
    if ( pipe( in ) < 0 ) {
      return;
    }
    if ( pipe( out ) < 0 ) {
      close( in[0] );
      close( in[1] );
      return;
    }

    std::vector<char*> argv;
    for ( size_t i = 0; i < options.command.size(); i++ ) {
      argv.push_back( const_cast<char*>( options.command[i].c_str() ) );
    }
    argv.push_back( NULL );

    child = fork();
    if ( child == 0 ) {
      own_signals();
      dup2( in[0], STDIN_FILENO );
      dup2( out[1], STDOUT_FILENO );
      const long limit = sysconf( _SC_OPEN_MAX );
      for ( int fd = 3; fd < ( limit > 0 && limit < 4096 ? limit : 4096 ); fd++ ) {
        close( fd );
      }
      execvp( argv[0], argv.data() );
      _exit( 127 );
    }
    close( in[0] );
    close( out[1] );
    if ( child < 0 ) {
      close( in[1] );
      close( out[0] );
      return;
    }
    to_child = in[1];
    from_child = out[0];
    fcntl( to_child, F_SETFL, fcntl( to_child, F_GETFL ) | O_NONBLOCK );
    paths.stream_started( true );
  }

  void flush( void )
  {
    while ( to_child >= 0 && !backlog.empty() ) {
      ssize_t n = write( to_child, backlog.data(), backlog.size() );
      if ( n > 0 ) {
        backlog.erase( 0, n );
      } else if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) ) {
        break;
      } else {
        stop_child(); /* it has gone; it will be started again when it is wanted */
        break;
      }
    }
  }

  void to_client( const char* data, size_t len )
  {
    if ( client_len ) {
      sendto( client_fd, data, len, MSG_DONTWAIT, &client.sa, client_len );
    }
  }

  void from_client( const std::string& datagram, uint64_t now )
  {
    if ( direct_fd >= 0 ) {
      sendto( direct_fd, datagram.data(), datagram.size(), MSG_DONTWAIT, &options.server.sa, options.server_len );
    }
    if ( punch && punch->open( now ) ) {
      punch->send( datagram );
    }

    /* Is either route by UDP answering?  If not, by the stream as well (paths.h). */
    const Gosh::LocalPaths::Also also = paths.from_client( now, direct_fd >= 0, punch ? punch->last_heard() : 0 );
    if ( also.start_stream ) {
      start_child();
    }
    if ( also.by_stream && to_child >= 0 && backlog.size() + datagram.size() <= STREAM_BACKLOG ) {
      backlog += relay_frame( datagram );
      flush();
    }
    if ( also.hop ) {
      open_direct(); /* a new port sometimes gets through where an old one has stopped */
    }
  }

  /* Words from the remote relay (see control(), above). */
  void from_remote( const std::vector<std::string>& words, uint64_t now )
  {
    if ( words.size() == 2 && words[0] == "HELLO" && punching && !punch ) {
      /* It knows how to look for a way through.  So: a key for the two of us, and where to ask. */
      try {
        const std::string key = Crypto::Base64Key().printable_key();
        punch.reset( new Punch( true, key, ask, now, meetup_key ) );
        std::string servers;
        for ( size_t i = 0; i < ask.size(); i++ ) {
          servers += " " + ask[i];
        }
        backlog += control( "STUN" + servers ) + ( meetup_key.empty() ? "" : control( "STUNKEY " + meetup_key ) )
                   + control( "KEY " + key )
                   + control( "PORT " + std::to_string( punch->local_port() ) );
        say( "looking for a way through, from port %d", static_cast<int>( punch->local_port() ) );
      } catch ( const std::exception& e ) {
        punch.reset();
      }
    } else if ( words.size() == 2 && words[0] == "PORT" && punch && options.server.sa.sa_family == AF_INET ) {
      /* at the server's own address, if nothing in between changes the port */
      const std::string server = format_ip_port( options.server );
      punch->add_candidate( server.substr( 0, server.rfind( ':' ) ) + ":" + words[1] );
    } else if ( words.size() == 2 && words[0] == "CAND" && punch ) {
      punch->add_candidate( words[1] );
    }
  }

  void tend_punch( uint64_t now )
  {
    if ( !punch ) {
      return;
    }
    std::string datagram;
    Punch::Got got;
    while ( ( got = punch->receive( now, datagram ) ) != Punch::NOTHING ) {
      if ( got == Punch::DATA ) {
        to_client( datagram.data(), datagram.size() );
      }
    }
    punch->tick( now );
    const std::vector<std::string> mine = punch->take_local_candidates();
    for ( size_t i = 0; i < mine.size(); i++ ) {
      say( "we may be reached at %s", mine[i].c_str() );
      if ( to_child >= 0 ) {
        backlog += control( "CAND " + mine[i] );
      }
    }
    if ( punch->open( now ) != was_open ) {
      was_open = punch->open( now );
      say( was_open ? "a way through is open, to %s" : "the way through to %s has closed", punch->where().c_str() );
    }
  }

  /* unused */
  LocalRelay( const LocalRelay& );
  LocalRelay& operator=( const LocalRelay& );

public:
  LocalRelay( const RelayOptions& s_options, int s_client_fd, int s_alive_fd )
    : options( s_options ), client_fd( s_client_fd ), direct_fd( -1 ), alive_fd( s_alive_fd ), client(),
      client_len( 0 ), child( -1 ), to_child( -1 ), from_child( -1 ), backlog(), parser(),
      paths( s_options.stream_only, !s_options.command.empty(), told( s_options.stream_only ).stream_idle ),
      punching( false ), ask(), meetup_key(), punch(), was_open( false )
  {
    open_direct();
    const Gosh::RelaySettings settings = told( options.stream_only );
    punching = settings.path_finding;
    ask = settings.ask;
    meetup_key = settings.meetup_key;
  }

  void run( void )
  {
    char buf[MAX_DATAGRAM + 1];
    std::string datagram;

    while ( true ) {
      /* (poll ignores a negative descriptor, so the absent ones need no special care) */
      struct pollfd fds[6] = { { alive_fd, POLLIN, 0 },
                               { client_fd, POLLIN, 0 },
                               { direct_fd, POLLIN, 0 },
                               { from_child, POLLIN, 0 },
                               { backlog.empty() ? -1 : to_child, POLLOUT, 0 },
                               { punch ? punch->fd() : -1, POLLIN, 0 } };
      if ( poll( fds, 6, punch ? 250 : 1000 ) < 0 && errno != EINTR ) {
        break;
      }
      const uint64_t now = now_ms();

      if ( fds[0].revents ) {
        /* the client has gone, and so should we: and the far end of the way through, without waiting to wonder */
        if ( punch ) {
          punch->goodbye();
        }
        break;
      }

      tend_punch( now );

      if ( fds[1].revents & POLLIN ) {
        Addr from;
        socklen_t from_len = sizeof( from );
        ssize_t n = recvfrom( client_fd, buf, sizeof( buf ), MSG_DONTWAIT, &from.sa, &from_len );
        if ( n >= 0 && from_our_client( from, from_len ) ) {
          client = from; /* it changes its port from time to time */
          client_len = from_len;
          from_client( std::string( buf, n ), now );
        }
      }

      if ( direct_fd >= 0 && ( fds[2].revents & POLLIN ) ) {
        /* (gosh) Only what comes from the server's own address is the server answering (whose.h). */
        Addr from;
        socklen_t from_len = sizeof( from );
        memset( &from, 0, sizeof( from ) );
        ssize_t n = recvfrom( direct_fd, buf, sizeof( buf ), MSG_DONTWAIT, &from.sa, &from_len );
        if ( n >= 0 && Gosh::same_udp_address( &from.sa, from_len, &options.server.sa, options.server_len ) ) {
          paths.direct_answered( now );
          to_client( buf, n );
        }
      }

      if ( from_child >= 0 && fds[3].revents ) {
        ssize_t n = read( from_child, buf, sizeof( buf ) );
        if ( n > 0 ) {
          parser.append( buf, n );
          std::vector<std::string> words;
          while ( parser.next( datagram ) ) {
            /* (gosh) It works when a whole frame has come: not at a byte, which a login that then fails may print. */
            paths.stream_answered();
            if ( is_control( datagram, words ) ) {
              from_remote( words, now );
            } else {
              to_client( datagram.data(), datagram.size() );
            }
          }
        } else if ( n == 0 || ( errno != EINTR && errno != EAGAIN ) ) {
          say( "the stream has ended" );
          stop_child();
        }
      }

      flush();

      /* The direct route has been enough for a while: let the other one go. */
      if ( paths.let_stream_go( now ) ) {
        say( "the stream has not been needed for a while: closing it" );
        stop_child();
      }
    }

    stop_child();
  }
};

}

int Network::relay_start_local( const RelayOptions& options, int& keep_alive_fd, pid_t& pid )
{
  /* where the client will find us */
  const int client_fd = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( client_fd < 0 ) {
    return 0;
  }
  struct sockaddr_in sin;
  memset( &sin, 0, sizeof( sin ) );
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  socklen_t len = sizeof( sin );
  int alive[2];
  if ( bind( client_fd, reinterpret_cast<struct sockaddr*>( &sin ), sizeof( sin ) ) < 0
       || getsockname( client_fd, reinterpret_cast<struct sockaddr*>( &sin ), &len ) < 0 || pipe( alive ) < 0 ) {
    close( client_fd );
    return 0;
  }

  pid = fork();
  if ( pid < 0 ) {
    close( client_fd );
    close( alive[0] );
    close( alive[1] );
    return 0;
  }

  if ( pid == 0 ) {
    /*
     * A process apart: nothing typed at the terminal should reach it or
     * the ssh it runs (an interrupt, say, while the client still has a
     * goodbye to send through them), and it has no business with the
     * terminal or anything else the client has open.
     */
    own_signals();
    setsid();
    signal( SIGPIPE, SIG_IGN );
    const int null = open( "/dev/null", O_RDWR );
    if ( null >= 0 ) {
      dup2( null, STDIN_FILENO );
      dup2( null, STDOUT_FILENO );
      dup2( null, STDERR_FILENO );
    }
    const long limit = sysconf( _SC_OPEN_MAX );
    for ( int fd = 3; fd < ( limit > 0 && limit < 4096 ? limit : 4096 ); fd++ ) {
      if ( fd != client_fd && fd != alive[0] ) {
        close( fd );
      }
    }
    relay_log_who = "local";
    LocalRelay relay( options, client_fd, alive[0] );
    relay.run();
    _exit( 0 );
  }

  close( client_fd );
  close( alive[0] );
  keep_alive_fd = alive[1];
  fcntl( keep_alive_fd, F_SETFD, FD_CLOEXEC );
  return ntohs( sin.sin_port );
}
