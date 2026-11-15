/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * relay, the remote relay: the process "gosh-server relay ADDRESS PORT", which ssh starts on
 * the server's machine (REQ1/PATH-1). An edge: sockets, standard input and output, the clock.
 * What it decides it asks of paths.h (which way an answer goes back, when it is finished) and
 * whose.h (which ways to the server to keep). The fork's text (mosh-1.4.0-pf.7,
 * src/network/relay.cc, relay_remote) but at the places marked (gosh).
 */

#include "src/relay/process.h"

int Network::relay_remote( const char* address, const char* port )
{
  /*
   * Where is the server, from here?  We are on its machine, and are
   * told the address the client uses for it.  But that can be an address
   * this machine does not know as its own, when something between
   * translates addresses, and then a datagram sent to it has to leave
   * and come back, past whatever was in UDP's way to begin with.  So try
   * also the address ssh reached this machine at, which is the one the
   * server usually listens on, and this machine's address for itself;
   * and stay with whichever answers.
   */
  std::vector<std::string> addresses;
  if ( const char* ssh_connection = getenv( "SSH_CONNECTION" ) ) {
    std::string as_ssh_reached_it;
    if ( Gosh::ssh_server_address( ssh_connection, as_ssh_reached_it ) ) {
      addresses.push_back( as_ssh_reached_it );
    }
  }
  addresses.push_back( address );
  addresses.push_back( "127.0.0.1" );
  addresses.push_back( "::1" );

  std::vector<int> sockets;
  for ( size_t i = 0; i < addresses.size(); i++ ) {
    bool repeated = false;
    for ( size_t j = 0; j < i; j++ ) {
      repeated = repeated || addresses[j] == addresses[i];
    }
    const int fd = repeated ? -1 : udp_to( addresses[i], port );
    if ( fd >= 0 ) {
      sockets.push_back( fd );
    }
  }
  if ( sockets.empty() ) {
    return 1;
  }

  /*
   * We may be wanted after the connection that started us has gone: if a
   * way straight through to the other relay is found (punch.h), that
   * connection is let go, and we are one end of the way through.
   */
  setsid();
  signal( SIGHUP, SIG_IGN );
  signal( SIGPIPE, SIG_IGN );
  relay_log_who = "remote";

  /* the client's address as ssh saw it: where its relay may be found, for all we know */
  std::string client_ip;
  if ( const char* ssh_connection = getenv( "SSH_CONNECTION" ) ) {
    client_ip = Gosh::ssh_client_address( ssh_connection );
  }

  RelayFrameParser parser;
  char buf[MAX_DATAGRAM + 1];
  std::string datagram;
  std::vector<struct pollfd> fds;
  std::vector<std::string> words, ask;
  std::string meetup_key;
  std::unique_ptr<Punch> punch;
  bool stream_open = true, was_open = false;
  uint64_t stream_heard = 0, stream_closed = 0;

  /*
   * (gosh) HELLO is what invites the local relay to look for a way through; one that is not
   * told it says nothing of the kind. While path-finding is off (MOSH_PUNCH=never, which gosh's
   * server sets until queue item 8 lands) it is not said, so no path-finding code runs here
   * whichever local relay is at the other end, and no server on the Internet is asked anything.
   */
  if ( Gosh::relay_settings( false, NULL, getenv( "MOSH_PUNCH" ), NULL, NULL, NULL ).path_finding
       && !write_all( STDOUT_FILENO, control( "HELLO 1" ) ) ) {
    return 0;
  }

  while ( true ) {
    fds.clear();
    const struct pollfd in = { stream_open ? STDIN_FILENO : -1, POLLIN, 0 };
    fds.push_back( in );
    const struct pollfd through = { punch ? punch->fd() : -1, POLLIN, 0 };
    fds.push_back( through );
    for ( size_t i = 0; i < sockets.size(); i++ ) {
      const struct pollfd udp = { sockets[i], POLLIN, 0 };
      fds.push_back( udp );
    }
    if ( poll( fds.data(), fds.size(), punch ? 250 : -1 ) < 0 ) {
      if ( errno == EINTR ) {
        continue;
      }
      return 1;
    }
    const uint64_t now = now_ms();
    bool stream_ended = false;

    if ( fds[0].revents ) {
      ssize_t n = read( STDIN_FILENO, buf, sizeof( buf ) );
      if ( n == 0 || ( n < 0 && errno != EINTR && errno != EAGAIN ) ) {
        stream_ended = true; /* the connection that brought us here has gone */
      }
      if ( n > 0 ) {
        parser.append( buf, n );
        while ( parser.next( datagram ) ) {
          if ( !is_control( datagram, words ) ) {
            stream_heard = now;
            for ( size_t i = 0; i < sockets.size(); i++ ) {
              /* if it can't go, it is lost, like any datagram */
              send( sockets[i], datagram.data(), datagram.size(), MSG_DONTWAIT );
            }
          } else if ( words.size() >= 1 && words[0] == "STUN" ) {
            ask.assign( words.begin() + 1, words.end() );
          } else if ( words.size() == 2 && words[0] == "STUNKEY" ) {
            meetup_key = words[1];
          } else if ( words.size() == 2 && words[0] == "KEY" ) {
            try {
              punch.reset( new Punch( false, words[1], ask, now, meetup_key ) );
              stream_ended = stream_ended || !write_all( STDOUT_FILENO, control( "PORT " + std::to_string( punch->local_port() ) ) );
              say( "looking for a way through, from port %d", static_cast<int>( punch->local_port() ) );
            } catch ( const std::exception& e ) {
              punch.reset();
            }
          } else if ( words.size() == 2 && words[0] == "PORT" && punch && is_ipv4( client_ip ) ) {
            punch->add_candidate( client_ip + ":" + words[1] );
          } else if ( words.size() == 2 && words[0] == "CAND" && punch ) {
            punch->add_candidate( words[1] );
          }
        }
      }
    }

    if ( punch ) {
      Punch::Got got;
      while ( ( got = punch->receive( now, datagram ) ) != Punch::NOTHING ) {
        if ( got == Punch::GOODBYE ) {
          say( "the other end has finished" );
          return 0;
        }
        if ( got == Punch::DATA ) {
          for ( size_t i = 0; i < sockets.size(); i++ ) {
            send( sockets[i], datagram.data(), datagram.size(), MSG_DONTWAIT );
          }
        }
      }
      punch->tick( now );
      const std::vector<std::string> mine = punch->take_local_candidates();
      for ( size_t i = 0; i < mine.size(); i++ ) {
        say( "we may be reached at %s", mine[i].c_str() );
        if ( stream_open && !stream_ended ) {
          stream_ended = !write_all( STDOUT_FILENO, control( "CAND " + mine[i] ) );
        }
      }
      if ( punch->open( now ) != was_open ) {
        was_open = punch->open( now );
        say( was_open ? "a way through is open, to %s" : "the way through to %s has closed", punch->where().c_str() );
      }
    }

    for ( size_t i = 0; i < sockets.size(); i++ ) {
      if ( !( fds[2 + i].revents & POLLIN ) ) {
        /* (an error on one of them, such as nobody being there, is read off and forgotten) */
        if ( fds[2 + i].revents ) {
          recv( sockets[i], buf, sizeof( buf ), MSG_DONTWAIT );
        }
        continue;
      }
      ssize_t n = recv( sockets[i], buf, sizeof( buf ), MSG_DONTWAIT );
      if ( n < 0 ) {
        continue;
      }
      /* From the server, for the client: by the way through if there is one, and by the stream if that is in use. */
      const std::string answer( buf, n );
      const bool through_open = punch && punch->open( now );
      if ( through_open ) {
        punch->send( answer );
      }
      if ( Gosh::remote_answer_by( now, through_open, stream_open && !stream_ended, stream_heard ).stream ) {
        stream_ended = !write_all( STDOUT_FILENO, relay_frame( answer ) );
      }
      /*
       * (gosh) The server is at this one, for all we know: the less likely ways after it can
       * go. The likelier ways before it are kept and still tried, so that whoever answers
       * first from a less likely place (127.0.0.1, say, which any user of this machine may
       * bind) does not shut out the server (whose.h).
       */
      const size_t keep = Gosh::remote_ways_kept( sockets.size(), i );
      if ( keep < sockets.size() ) {
        for ( size_t j = keep; j < sockets.size(); j++ ) {
          close( sockets[j] );
        }
        sockets.resize( keep );
        break;
      }
    }

    if ( stream_ended && stream_open ) {
      stream_open = false;
      stream_closed = now;
      if ( !( punch && punch->open( now ) ) ) {
        return 0;
      }
      /*
       * Stay, as one end of the way through: but not as the process that
       * was started, which whoever started it (sshd, say) may be waiting
       * to see finish.  The stream's descriptors are no more use.
       */
      say( "the stream has ended; staying for the way through" );
      if ( fork() > 0 ) {
        _exit( 0 );
      }
      const int null = open( "/dev/null", O_RDWR );
      if ( null >= 0 ) {
        dup2( null, STDIN_FILENO );
        dup2( null, STDOUT_FILENO );
        dup2( null, STDERR_FILENO );
        if ( null > STDERR_FILENO ) {
          close( null );
        }
      }
    }
    if ( !stream_open && Gosh::remote_finished( now, punch->last_heard(), stream_closed ) ) {
      say( "nothing heard for a minute: finished" );
      return 0;
    }
  }
}
