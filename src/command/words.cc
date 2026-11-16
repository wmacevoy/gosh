/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#include "src/command/words.h"

#include <cstdio>
#include <cstdlib>

#include "src/command/stock.h"

Gosh::Command::Words Gosh::Command::shell_words( const std::string& line )
{
  Words words;
  std::string word;
  bool in_word = false;
  char quote = 0;
  for ( size_t i = 0; i < line.size(); i++ ) {
    const char c = line[i];
    if ( quote ) {
      if ( c == quote ) {
        quote = 0;
      } else if ( c == '\\' && quote == '"' && i + 1 < line.size() ) {
        word.push_back( line[++i] );
      } else {
        word.push_back( c );
      }
    } else if ( c == '\'' || c == '"' ) {
      quote = c;
      in_word = true;
    } else if ( c == '\\' && i + 1 < line.size() ) {
      word.push_back( line[++i] );
      in_word = true;
    } else if ( c == ' ' || c == '\t' || c == '\n' ) {
      if ( in_word ) {
        words.push_back( word );
        word.clear();
        in_word = false;
      }
    } else {
      word.push_back( c );
      in_word = true;
    }
  }
  if ( in_word ) {
    words.push_back( word );
  }
  return words;
}

std::string Gosh::Command::shell_quote( const Words& words )
{
  std::string s;
  for ( size_t i = 0; i < words.size(); i++ ) {
    s += i ? " '" : "'";
    for ( size_t j = 0; j < words[i].size(); j++ ) {
      if ( words[i][j] == '\'' ) {
        s += "'\\''";
      } else {
        s.push_back( words[i][j] );
      }
    }
    s += "'";
  }
  return s;
}

bool Gosh::Command::remote_place( const std::string& word, std::string& host, std::string& path )
{
  const size_t colon = word.find( ':' );
  if ( colon == std::string::npos || colon < 2 || word.find( '/' ) < colon ) {
    return false;
  }
  host = word.substr( 0, colon );
  path = word.substr( colon + 1 );
  return true;
}

Gosh::Command::Asked Gosh::Command::what_was_asked( const Settings& s, bool copy, bool stdio, const std::string& name,
                                    const char* copy_usage )
{
  Asked session;
  session.headless = s.flag( "N" );
  Words rest = s.rest;

  if ( copy ) {
    /* Copying files: work out which end is which, and what each is to do. */
    if ( rest.size() < 2 ) {
      throw Failure( copy_usage );
    }
    Words flags;
    if ( s.flag( "r" ) ) {
      flags.push_back( "-r" );
    }
    if ( s.flag( "auto" ) ) {
      flags.push_back( "--auto" );
    }
    if ( s.flag( "delete" ) ) {
      flags.push_back( "--delete" );
    }
    if ( s.has( "interval" ) ) {
      const double seconds = atof( s.value( "interval" ).c_str() );
      if ( seconds < 0.1 ) {
        throw Failure( name + ": --interval must be at least 0.1 (seconds)." );
      }
      char word[40];
      snprintf( word, sizeof( word ), "--interval=%d", static_cast<int>( seconds * 1000 ) );
      flags.push_back( word );
    }
    Words here = flags;
    if ( s.flag( "q" ) ) {
      here.push_back( "-q" );
    }

    Words hosts, paths;
    for ( size_t i = 0; i < rest.size(); i++ ) {
      std::string host, path = rest[i];
      remote_place( rest[i], host, path );
      if ( path.find( '\x1f' ) != std::string::npos ) {
        throw Failure( name + ": a file name cannot contain the character with code 31." );
      }
      hosts.push_back( host );
      paths.push_back( path );
    }
    const std::string dest_host = hosts.back(), dest_path = paths.back();
    hosts.pop_back();
    paths.pop_back();

    std::string host;
    session.stream_local.push_back( "@copy" );
    session.stream_remote.push_back( "@copy" );
    if ( !dest_host.empty() ) {
      for ( size_t i = 0; i < hosts.size(); i++ ) {
        if ( !hosts[i].empty() ) {
          throw Failure( name + ": one end of a copy has to be this machine." );
        }
      }
      host = dest_host;
      session.stream_local.push_back( "send" );
      session.stream_local.insert( session.stream_local.end(), here.begin(), here.end() );
      session.stream_local.push_back( "--" );
      session.stream_local.insert( session.stream_local.end(), paths.begin(), paths.end() );
      session.stream_remote.push_back( "receive" );
      session.stream_remote.insert( session.stream_remote.end(), flags.begin(), flags.end() );
      session.stream_remote.push_back( "--" );
      session.stream_remote.push_back( dest_path.empty() ? "." : dest_path );
    } else {
      host = hosts[0];
      if ( host.empty() ) {
        throw Failure( name + ": one end of a copy has to be another machine: [user@]host:path" );
      }
      for ( size_t i = 0; i < hosts.size(); i++ ) {
        if ( hosts[i] != host ) {
          throw Failure( name + ": the sources have to be on one machine." );
        }
        if ( paths[i].empty() ) {
          throw Failure( name + ": what is to be copied from " + host + "?" );
        }
      }
      /* (patterns are for the other end to expand, as a shell there would) */
      session.stream_remote.push_back( "send" );
      session.stream_remote.insert( session.stream_remote.end(), flags.begin(), flags.end() );
      session.stream_remote.push_back( "--glob" );
      session.stream_remote.push_back( "--" );
      session.stream_remote.insert( session.stream_remote.end(), paths.begin(), paths.end() );
      session.stream_local.push_back( "receive" );
      session.stream_local.insert( session.stream_local.end(), here.begin(), here.end() );
      session.stream_local.push_back( "--" );
      session.stream_local.push_back( dest_path );
    }
    rest.assign( 1, host );
    session.headless = true;
  } else if ( stdio ) {
    /* (the command may be set off with "--") */
    if ( rest.size() > 1 && rest[1] == "--" ) {
      rest.erase( rest.begin() + 1 );
    }
    if ( rest.size() < 2 ) {
      throw Failure( name + ": --stdio needs a command to run on the server.", true );
    }
    /* as ssh would: the words joined, for the shell at the other end */
    session.stream_remote.push_back( "@sh" );
    session.stream_remote.push_back( join( Words( rest.begin() + 1, rest.end() ), " " ) );
    session.stream_local.push_back( "@stdio" );
    if ( s.has( "l" ) && rest[0].find( '@' ) == std::string::npos ) {
      rest[0] = s.value( "l" ) + "@" + rest[0];
    }
    rest.resize( 1 );
    session.headless = true;
  }

  if ( rest.empty() ) {
    throw Failure( "", true );
  }
  session.host = rest[0];
  session.command.assign( rest.begin() + 1, rest.end() );
  /* No terminal, so nothing to run in it: so, unless told, something that waits. */
  if ( session.headless && session.command.empty() ) {
    session.command.push_back( "sh" );
    session.command.push_back( "-c" );
    session.command.push_back( "while :; do sleep 86400; done" );
  }
  return session;
}

Gosh::Command::Words Gosh::Command::bind_arguments( const std::string& bind, bool local, const std::string& userhost )
{
  Words words;
  if ( bind.empty() || lower( bind ) == "ssh" ) {
    if ( local ) {
      words.push_back( "-i" );
      words.push_back( userhost );
    } else {
      words.push_back( "-s" );
    }
  } else if ( lower( bind ) != "any" ) {
    words.push_back( "-i" );
    words.push_back( bind );
  }
  return words;
}

Gosh::Command::ServerWords Gosh::Command::server_words( const Settings& s, const Choices& choices, const Asked& session,
                                      const std::string& colors, const Words& bind, const Words& locales )
{
  ServerWords w;
  w.gosh.push_back( "new" );
  w.gosh.push_back( "-c" );
  w.gosh.push_back( colors );
  w.gosh.insert( w.gosh.end(), bind.begin(), bind.end() );
  if ( s.has( "port" ) ) {
    w.gosh.push_back( "-p" );
    w.gosh.push_back( s.value( "port" ) );
  }
  for ( size_t i = 0; i < locales.size(); i++ ) {
    w.gosh.push_back( "-l" );
    w.gosh.push_back( locales[i] );
  }
  const char* const forward_options[][2] = { { "udp-forward", "-LU" }, { "udp-remote-forward", "-RU" },
                                             { "tcp-forward", "-L" }, { "tcp-remote-forward", "-R" } };
  for ( size_t i = 0; i < 4; i++ ) {
    const Words specs = s.all( forward_options[i][0] );
    for ( size_t j = 0; j < specs.size(); j++ ) {
      w.forwards.push_back( forward_options[i][1] );
      w.forwards.push_back( specs[j] );
    }
  }
  if ( !choices.fragment_parity.empty() ) {
    w.forwards.push_back( "-F" );
    w.forwards.push_back( choices.fragment_parity );
  }
  /* (gosh, REQ8/CMD-3) what a stock mosh-server is asked: the same, less what only gosh's knows */
  w.stock = w.gosh;
  w.gosh.insert( w.gosh.end(), w.forwards.begin(), w.forwards.end() );
  if ( !session.stream_remote.empty() ) {
    w.gosh.push_back( "-X" );
    w.gosh.push_back( join( session.stream_remote, "\x1f" ) );
  }
  if ( !session.command.empty() ) {
    w.gosh.push_back( "--" );
    w.gosh.insert( w.gosh.end(), session.command.begin(), session.command.end() );
    w.stock.push_back( "--" );
    w.stock.insert( w.stock.end(), session.command.begin(), session.command.end() );
  }
  return w;
}

std::string Gosh::Command::server_line( bool pick_server, const std::string& server, const ServerWords& words )
{
  /*
   * (gosh, REQ8/CMD-3) Unless the user named a server, the host is asked for gosh-server and,
   * where it has none, for mosh-server, and says which it started. A server the user named is
   * taken to be gosh's and is asked for everything, as before.
   */
  return pick_server ? server_command( shell_quote( words.gosh ), shell_quote( words.stock ) )
                     : server + " " + shell_quote( words.gosh );
}

Gosh::Command::Words Gosh::Command::login_words( const Words& ssh, bool ssh_pty, const Choices& choices, const std::string& proxy,
                               const std::string& userhost, const std::string& server_line )
{
  Words words = ssh;
  words.push_back( "-n" );
  if ( ssh_pty ) {
    words.push_back( "-tt" );
  }
  std::string ask_address;
  if ( choices.remote_ip == "remote" ) {
    /* Ask the server for its address.  The login shell may not be a Bourne shell, so say which. */
    ask_address = "sh -c "
                  + shell_quote( Words(
                    1, "[ -n \"$SSH_CONNECTION\" ] && printf \"\\nMOSH SSH_CONNECTION %s\\n\" \"$SSH_CONNECTION\"" ) )
                  + " ; ";
    if ( choices.family == "inet" ) {
      words.push_back( "-4" );
    } else if ( choices.family == "inet6" ) {
      words.push_back( "-6" );
    }
  } else if ( choices.remote_ip == "proxy" ) {
    words.push_back( "-S" );
    words.push_back( "none" );
    words.push_back( "-o" );
    words.push_back( "ProxyCommand=" + proxy );
  }
  words.push_back( userhost );
  words.push_back( "--" );
  words.push_back( ask_address + server_line );
  return words;
}

Gosh::Command::Words Gosh::Command::relay_command( const Words& ssh, bool local, bool pick_server, const std::string& server,
                                 const std::string& userhost, const std::string& ip, const std::string& port )
{
  /* (gosh, REQ1/PATH-1) gosh-server's relay, unless the user named the server: never mosh-server by name */
  const std::string relay_words = "relay " + shell_quote( Words( 1, ip ) ) + " " + shell_quote( Words( 1, port ) );
  const std::string remote = pick_server ? helper_command( relay_words ) : server + " " + relay_words;
  Words words;
  if ( local ) {
    words.push_back( "sh" );
    words.push_back( "-c" );
    words.push_back( remote );
  } else {
    /* ssh must not ask for anything, because the terminal will be the session's by then */
    words = ssh;
    words.push_back( "-T" );
    words.push_back( "-o" );
    words.push_back( "BatchMode=yes" );
    words.push_back( userhost );
    words.push_back( "--" );
    words.push_back( remote );
  }
  return words;
}

Gosh::Command::Words Gosh::Command::ticket_command( const Words& ssh, bool pick_server, const std::string& server,
                                  const std::string& meetup_login )
{
  Words fetch = ssh;
  fetch.push_back( "-T" );
  fetch.push_back( "-o" );
  fetch.push_back( "BatchMode=yes" );
  fetch.push_back( meetup_login );
  fetch.push_back( "--" );
  fetch.push_back( pick_server ? helper_command( "meetup-ticket" ) : server + " meetup-ticket" );
  return fetch;
}

Gosh::Command::Words Gosh::Command::client_words( const Words& args, const Words& forwards, const Asked& session, const std::string& ip,
                                const std::string& port )
{
  Words words;
  words.push_back( "mosh-client" );
  words.push_back( "-# " + join( args, " " ) + " |" ); /* (for whoever lists processes: what was asked) */
  words.insert( words.end(), forwards.begin(), forwards.end() );
  if ( !session.stream_local.empty() ) {
    words.push_back( "-X" );
    words.push_back( join( session.stream_local, "\x1f" ) );
  } else if ( session.headless ) {
    words.push_back( "-N" );
  }
  words.push_back( ip );
  words.push_back( port );
  return words;
}

Gosh::Command::Variables Gosh::Command::client_variables( const std::string& key, const Choices& choices, bool init, bool predict_overwrite )
{
  Variables v;
  v.push_back( std::make_pair( "MOSH_KEY", key ) );
  v.push_back( std::make_pair( "MOSH_PREDICTION_DISPLAY", choices.predict ) );
  if ( !init ) {
    v.push_back( std::make_pair( "MOSH_NO_TERM_INIT", "1" ) );
  }
  if ( predict_overwrite ) {
    v.push_back( std::make_pair( "MOSH_PREDICTION_OVERWRITE", "yes" ) );
  }
  return v;
}

Gosh::Command::Variables Gosh::Command::relay_variables( const Choices& choices, bool stock, const Words& relay, const Words& ticket )
{
  Variables v;
  /*
   * If UDP will not reach the server, the client can carry the session over ssh instead, by
   * running a relay on the server. (gosh, REQ8/CMD-3) A stock server has no relay to start.
   */
  if ( choices.relay != "never" && !stock ) {
    v.push_back( std::make_pair( "GOSH_RELAY_COMMAND", join( relay, "\x1f" ) ) );
    v.push_back( std::make_pair( "GOSH_RELAY", choices.relay ) );
    /* (gosh, REQ15/PATH-4) How the local relay gets a ticket for the meetup: ssh to it, as to the server. */
    if ( !choices.meetup_login.empty() ) {
      v.push_back( std::make_pair( "GOSH_MEETUP_TICKET_COMMAND", join( ticket, "\x1f" ) ) );
    }
  } else {
    v.push_back( std::make_pair( "GOSH_RELAY", "never" ) );
  }
  return v;
}
