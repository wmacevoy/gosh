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
  std::unique_ptr<Punch> punch;
  bool was_open, was_registered;
  std::string was_where; /* (gosh) where the way through was, last we said */

  /*
   * (gosh) The meetup (REQ15/PATH-4): a ticket for it is fetched over ssh, by a child that first
   * finds the meetup's address and prints it, and then runs the ticket's command. Once, when
   * path-finding first starts; the ticket is good for 30 days, longer than most sessions.
   */
  std::string meetup;                      /* "host:port", as the user named it */
  std::vector<std::string> ticket_command; /* ssh to it, and "gosh-server meetup-ticket" */
  pid_t ticket_child;
  int ticket_fd;
  std::string ticket_out;
  bool ticket_tried;
  std::string meetup_at; /* its address, "ip:port", once found */
  Gosh::Ticket ticket;   /* empty until fetched */

  /* (gosh) What the environment tells the relay: read here, and made sense of in settings.h. */
  static Gosh::RelaySettings told( bool stream_only )
  {
    return Gosh::relay_settings( stream_only, getenv( "GOSH_RELAY_STREAM_IDLE_MS" ), getenv( "GOSH_PUNCH" ),
                                 getenv( "GOSH_MEETUP" ), getenv( "GOSH_STUN" ), getenv( "GOSH_MEETUP_TICKET_COMMAND" ) );
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
      say( "local", "a datagram on the client's socket from port %d, which is not a socket of ours: not passed on",
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
    was_open = was_registered = false;
    say( "local", "starting the stream" );

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

  /* (gosh) Starts the child that fetches a ticket for the meetup, once. */
  void fetch_ticket( void )
  {
    if ( ticket_tried || ticket_command.empty() || meetup.empty() ) {
      return;
    }
    ticket_tried = true;
    int out[2];
    if ( pipe( out ) < 0 ) {
      return;
    }
    std::vector<char*> argv;
    for ( size_t i = 0; i < ticket_command.size(); i++ ) {
      argv.push_back( const_cast<char*>( ticket_command[i].c_str() ) );
    }
    argv.push_back( NULL );
    ticket_child = fork();
    if ( ticket_child == 0 ) {
      own_signals();
      dup2( out[1], STDOUT_FILENO );
      const int null = open( "/dev/null", O_RDWR );
      if ( null >= 0 ) {
        dup2( null, STDIN_FILENO );
      }
      const long limit = sysconf( _SC_OPEN_MAX );
      for ( int fd = 3; fd < ( limit > 0 && limit < 4096 ? limit : 4096 ); fd++ ) {
        close( fd );
      }
      /* where the meetup is: looked up here, where waiting on a name costs the relay nothing */
      const size_t colon = meetup.rfind( ':' );
      std::string host = colon == std::string::npos ? std::string() : meetup.substr( 0, colon );
      if ( host.size() > 2 && host[0] == '[' && host[host.size() - 1] == ']' ) {
        host = host.substr( 1, host.size() - 2 );
      }
      struct addrinfo hints, *res = NULL;
      memset( &hints, 0, sizeof( hints ) );
      hints.ai_family = AF_UNSPEC; /* (REQ17/PATH-8: the system's own order, IPv6 first where it has it) */
      hints.ai_socktype = SOCK_DGRAM;
      if ( colon == std::string::npos
           || getaddrinfo( host.c_str(), meetup.substr( colon + 1 ).c_str(), &hints, &res ) != 0 ) {
        _exit( 2 );
      }
      Addr found;
      memset( &found, 0, sizeof( found ) );
      memcpy( &found, res->ai_addr, res->ai_addrlen < sizeof( found ) ? res->ai_addrlen : sizeof( found ) );
      const std::string line = "MEETUP " + format_ip_port( found ) + "\n";
      if ( write( STDOUT_FILENO, line.data(), line.size() ) != static_cast<ssize_t>( line.size() ) ) {
        _exit( 2 );
      }
      execvp( argv[0], argv.data() );
      _exit( 127 );
    }
    close( out[1] );
    if ( ticket_child < 0 ) {
      close( out[0] );
      return;
    }
    ticket_fd = out[0];
    say( "local", "fetching a ticket for the meetup at %s", meetup.c_str() );
  }

  /* (gosh) What the ticket's child printed, once it has finished. */
  void ticket_fetched( void )
  {
    close( ticket_fd );
    ticket_fd = -1;
    int status = 0;
    waitpid( ticket_child, &status, 0 );
    ticket_child = -1;
    std::string at, text;
    size_t start = 0;
    while ( start < ticket_out.size() ) {
      size_t end = ticket_out.find( '\n', start );
      if ( end == std::string::npos ) {
        end = ticket_out.size();
      }
      const std::string line = ticket_out.substr( start, end - start );
      if ( line.compare( 0, 7, "MEETUP " ) == 0 ) {
        at = line.substr( 7 );
      } else if ( line.compare( 0, 12, "gosh-ticket " ) == 0 ) {
        text = line;
      }
      start = end + 1;
    }
    ticket_out.clear();
    Gosh::Ticket t;
    if ( at.empty() || !Gosh::parse_ticket( text, t ) ) {
      say( "local", "no ticket for the meetup at %s (could ssh log in there, and run gosh-server meetup-ticket?)", meetup.c_str() );
      return;
    }
    meetup_at = at;
    ticket = t;
    say( "local", "a ticket for the meetup at %s", meetup_at.c_str() );
    meet();
  }

  /* (gosh) The meetup, to our way through and the remote relay's, once both a ticket and a way through are here. */
  void meet( void )
  {
    if ( !punch || ticket.id.empty() || !punch->use_meetup( meetup_at, ticket ) ) {
      return;
    }
    backlog += control( "MEETUP " + meetup_at ) + control( "TICKET " + Gosh::ticket_text( ticket ).substr( 12 ) );
  }

  /* Words from the remote relay (see control(), above). */
  void from_remote( const std::vector<std::string>& words, uint64_t now )
  {
    if ( words.size() == 2 && words[0] == "HELLO" && punching && !punch ) {
      /* It knows how to look for a way through.  So: a key for the two of us, and where to ask. */
      try {
        const std::string key = Crypto::Base64Key().printable_key();
        punch.reset( new Punch( true, key, ask, now ) );
        std::string servers;
        for ( size_t i = 0; i < ask.size(); i++ ) {
          servers += " " + ask[i];
        }
        backlog += control( "STUN" + servers ) + control( "KEY " + key )
                   + control( "PORT " + std::to_string( punch->local_port() ) );
        say( "local", "looking for a way through, from port %d", static_cast<int>( punch->local_port() ) );
        meet();
        fetch_ticket();
      } catch ( const std::exception& e ) {
        punch.reset();
      }
    } else if ( words.size() == 2 && words[0] == "PORT" && punch
                && ( options.server.sa.sa_family == AF_INET || options.server.sa.sa_family == AF_INET6 ) ) {
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
      say( "local", "we may be reached at %s", mine[i].c_str() );
      if ( to_child >= 0 ) {
        backlog += control( "CAND " + mine[i] );
      }
    }
    if ( punch->open( now ) != was_open ) {
      was_open = punch->open( now );
      say( "local", was_open ? "a way through is open, to %s" : "the way through to %s has closed", punch->where().c_str() );
    } else if ( was_open && punch->where() != was_where ) {
      say( "local", "the way through is now to %s", punch->where().c_str() ); /* (gosh, REQ17/PATH-8: as by IPv6, from IPv4) */
    }
    was_where = punch->where();
    if ( punch->meetup_registered() != was_registered ) {
      was_registered = punch->meetup_registered();
      say( "local", was_registered ? "registered with the meetup at %s" : "no longer registered with the meetup at %s", meetup_at.c_str() );
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
      punching( false ), ask(), punch(), was_open( false ), was_registered( false ), was_where(), meetup(), ticket_command(), ticket_child( -1 ),
      ticket_fd( -1 ), ticket_out(), ticket_tried( false ), meetup_at(), ticket()
  {
    open_direct();
    const Gosh::RelaySettings settings = told( options.stream_only );
    punching = settings.path_finding;
    ask = settings.ask;
    meetup = settings.meetup;
    ticket_command = settings.ticket_command;
  }

  void run( void )
  {
    char buf[MAX_DATAGRAM + 1];
    std::string datagram;

    while ( true ) {
      /* (poll ignores a negative descriptor, so the absent ones need no special care) */
      struct pollfd fds[7] = { { alive_fd, POLLIN, 0 },
                               { client_fd, POLLIN, 0 },
                               { direct_fd, POLLIN, 0 },
                               { from_child, POLLIN, 0 },
                               { backlog.empty() ? -1 : to_child, POLLOUT, 0 },
                               { punch ? punch->fd() : -1, POLLIN, 0 },
                               { ticket_fd, POLLIN, 0 } };
      if ( poll( fds, 7, punch ? 250 : 1000 ) < 0 && errno != EINTR ) {
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

      if ( ticket_fd >= 0 && fds[6].revents ) {
        const ssize_t n = read( ticket_fd, buf, sizeof( buf ) );
        if ( n > 0 && ticket_out.size() < 4096 ) {
          ticket_out.append( buf, n );
        } else if ( n == 0 || ( n < 0 && errno != EINTR && errno != EAGAIN ) ) {
          ticket_fetched();
        }
      }

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
          say( "local", "the stream has ended" );
          stop_child();
        }
      }

      flush();

      /*
       * The direct route has been enough for a while: let the other one go. (gosh) But not while
       * the way is through the meetup: that is still fallen back, and keeps its ssh connection
       * ("meetup counts as fallen back", REQ15/PATH-4).
       */
      const bool through_meetup = punch && punch->open( now ) && !punch->direct();
      if ( !through_meetup && paths.let_stream_go( now ) ) {
        say( "local", "the stream has not been needed for a while: closing it" );
        stop_child();
      }
    }

    stop_child();
    if ( ticket_child > 0 ) {
      kill( ticket_child, SIGTERM );
      waitpid( ticket_child, NULL, 0 );
    }
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
