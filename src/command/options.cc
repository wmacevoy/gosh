/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#include "src/command/options.h"

#include <cstdlib>

namespace {

struct Option
{
  const char* names; /* separated by | */
  bool takes_value;
  bool negatable;
};

const Option OPTIONS[] = {
  { "client", true, false },
  { "server", true, false },
  { "predict", true, false },
  { "predict-overwrite|o", false, true },
  { "port|p", true, false },
  { "a", false, false },
  { "n", false, false },
  { "family", true, false },
  { "4", false, false },
  { "6", false, false },
  { "ssh", true, false },
  { "ssh-pty", false, true },
  { "init", false, true },
  { "local", false, false },
  { "help", false, false },
  { "version", false, false },
  { "bind-server", true, false },
  { "udp-forward|LU", true, false },
  { "udp-remote-forward|RU", true, false },
  { "tcp-forward|L", true, false },
  { "tcp-remote-forward|R", true, false },
  { "fragment-parity", true, false },
  { "no-fragment-parity", false, false },
  { "N", false, false },
  { "relay", true, false },
  { "punch", true, false },
  { "stun", true, false },
  { "meetup", true, false },
  { "copy", false, false },
  { "r", false, false },
  { "auto", false, false },
  { "delete", false, false },
  { "q", false, false },
  { "interval", true, false },
  { "stdio", false, false },
  { "l", true, false },
  { "experimental-remote-ip", true, false },
};

/* The option a word names (by its first name), and whether it was negated; NULL if none or several. */
const Option* find_option( const std::string& word, bool double_dash, bool& negated )
{
  negated = false;
  const Option* prefix_match = NULL;
  int prefix_matches = 0;
  for ( size_t i = 0; i < sizeof( OPTIONS ) / sizeof( OPTIONS[0] ); i++ ) {
    const std::vector<std::string> names = Gosh::Command::split( OPTIONS[i].names, '|' );
    for ( size_t j = 0; j < names.size(); j++ ) {
      if ( word == names[j] ) {
        return &OPTIONS[i];
      }
      if ( OPTIONS[i].negatable && ( word == "no" + names[j] || word == "no-" + names[j] ) ) {
        negated = true;
        return &OPTIONS[i];
      }
      /* a long name may be cut short, if what is left is still only one */
      if ( double_dash && names[j].size() > 1 && word.size() > 1 && names[j].compare( 0, word.size(), word ) == 0 ) {
        if ( prefix_match != &OPTIONS[i] ) {
          prefix_match = &OPTIONS[i];
          prefix_matches++;
        }
      }
    }
  }
  return prefix_matches == 1 ? prefix_match : NULL;
}

}

std::vector<std::string> Gosh::Command::split( const std::string& s, char at )
{
  std::vector<std::string> parts;
  size_t start = 0;
  while ( true ) {
    const size_t end = s.find( at, start );
    parts.push_back( s.substr( start, end == std::string::npos ? end : end - start ) );
    if ( end == std::string::npos ) {
      return parts;
    }
    start = end + 1;
  }
}

std::string Gosh::Command::join( const std::vector<std::string>& words, const std::string& with )
{
  std::string s;
  for ( size_t i = 0; i < words.size(); i++ ) {
    s += ( i ? with : "" ) + words[i];
  }
  return s;
}

std::string Gosh::Command::strip_end( std::string s )
{
  while ( !s.empty() && ( s[s.size() - 1] == '\r' || s[s.size() - 1] == '\n' || s[s.size() - 1] == ' ' ) ) {
    s.erase( s.size() - 1 );
  }
  return s;
}

bool Gosh::Command::all_digits( const std::string& s )
{
  return !s.empty() && s.find_first_not_of( "0123456789" ) == std::string::npos;
}

std::string Gosh::Command::lower( std::string s )
{
  for ( size_t i = 0; i < s.size(); i++ ) {
    s[i] = static_cast<char>( tolower( static_cast<unsigned char>( s[i] ) ) );
  }
  return s;
}

bool Gosh::Command::one_of( const std::string& word, const char* choices )
{
  const std::vector<std::string> all = split( choices, ' ' );
  for ( size_t i = 0; i < all.size(); i++ ) {
    if ( word == all[i] ) {
      return true;
    }
  }
  return false;
}

bool Gosh::Command::wants_stdio( const std::vector<std::string>& args )
{
  bool stdio = false;
  for ( size_t i = 0; i < args.size() && args[i] != "--"; i++ ) {
    stdio = stdio || args[i] == "--stdio";
  }
  return stdio;
}

void Gosh::Command::parse_options( const std::vector<std::string>& args, bool in_order, Settings& settings )
{
  for ( size_t i = 0; i < args.size(); i++ ) {
    const std::string& arg = args[i];
    if ( arg == "--" ) {
      /* (kept, in order: with --stdio it may stand between the host and the command) */
      if ( in_order ) {
        settings.rest.insert( settings.rest.end(), args.begin() + i, args.end() );
      } else {
        settings.rest.insert( settings.rest.end(), args.begin() + i + 1, args.end() );
      }
      return;
    }
    if ( arg.size() < 2 || arg[0] != '-' ) {
      if ( in_order ) {
        settings.rest.insert( settings.rest.end(), args.begin() + i, args.end() );
        return;
      }
      settings.rest.push_back( arg );
      continue;
    }
    const bool double_dash = arg[1] == '-';
    std::string word = arg.substr( double_dash ? 2 : 1 );
    std::string value;
    const size_t equals = word.find( '=' );
    const bool has_value = equals != std::string::npos;
    if ( has_value ) {
      value = word.substr( equals + 1 );
      word = word.substr( 0, equals );
    }
    bool negated;
    const Option* option = find_option( word, double_dash, negated );
    if ( !option ) {
      throw Failure( "Unknown option: " + word, true );
    }
    const std::string name = split( option->names, '|' )[0];
    if ( option->takes_value ) {
      if ( !has_value ) {
        if ( i + 1 >= args.size() ) {
          throw Failure( "Option " + word + " requires an argument", true );
        }
        value = args[++i];
      }
      settings.values[name].push_back( value );
    } else {
      if ( has_value ) {
        throw Failure( "Option " + word + " does not take an argument", true );
      }
      settings.flags[name] = !negated;
    }
  }
}

Gosh::Command::Choices Gosh::Command::choose( const Settings& s, const std::string& name, const char* prediction )
{
  Choices c;
  c.fragment_parity = s.value( "fragment-parity" );
  if ( s.flag( "no-fragment-parity" ) ) {
    c.fragment_parity = "never";
  }
  if ( !c.fragment_parity.empty() && !one_of( c.fragment_parity, "auto always never" ) ) {
    throw Failure( name + ": Unknown fragment parity mode \"" + c.fragment_parity + "\".", true );
  }
  c.relay = s.value( "relay", "auto" );
  if ( !one_of( c.relay, "auto always never" ) ) {
    throw Failure( name + ": Unknown relay mode \"" + c.relay + "\".", true );
  }

  c.punch = s.value( "punch", "auto" );
  if ( !one_of( c.punch, "auto never" ) ) {
    throw Failure( name + ": Unknown punch mode \"" + c.punch + "\".", true );
  }
  /* (the relays read these for themselves: see src/relay/settings.h) */
  if ( s.has( "punch" ) ) {
    c.relays_read.push_back( std::make_pair( "GOSH_PUNCH", c.punch ) );
  }
  if ( s.has( "stun" ) ) {
    c.relays_read.push_back( std::make_pair( "GOSH_STUN", s.value( "stun" ) ) );
  }
  /* (gosh) The meetup's address for UDP is its host and port; its ssh login, all before the port. */
  /* (REQ15/PATH-4, REQ1/PATH-7) --stun=none keeps the session off the meetup: no ticket is fetched */
  if ( s.has( "meetup" ) && s.value( "stun" ) != "none" ) {
    const std::string meetup = s.value( "meetup" );
    const size_t colon = meetup.rfind( ':' );
    /* (REQ16/PATH-8) HOST is a name, an IPv4 address, or an IPv6 one in brackets; PORT is 1 to 65535 */
    const size_t user = meetup.rfind( '@', colon == std::string::npos ? std::string::npos : colon );
    const std::string host = colon == std::string::npos ? std::string()
                                                        : meetup.substr( user == std::string::npos ? 0 : user + 1,
                                                                         colon - ( user == std::string::npos ? 0 : user + 1 ) );
    const std::string port = colon == std::string::npos ? std::string() : meetup.substr( colon + 1 );
    const bool bracketed = host.size() > 2 && host[0] == '[' && host[host.size() - 1] == ']'
                           && host.find_first_of( "[]", 1 ) == host.size() - 1;
    const bool plain = !host.empty() && host.find_first_of( ":[]" ) == std::string::npos;
    if ( !( bracketed || plain ) || port.size() > 5 || port.find_first_not_of( "0123456789" ) != std::string::npos
         || atol( port.c_str() ) < 1 || atol( port.c_str() ) > 65535 ) {
      throw Failure( name + ": --meetup needs HOST:PORT.", true );
    }
    c.meetup_login = meetup.substr( 0, colon );
    const size_t at = c.meetup_login.rfind( '@' );
    c.relays_read.push_back( std::make_pair( "GOSH_MEETUP", meetup.substr( at == std::string::npos ? 0 : at + 1 ) ) );
    /* (REQ16/PATH-8) "[2001:db8::1]:PORT": ssh takes the address without its brackets */
    const size_t open = c.meetup_login.find( '[', at == std::string::npos ? 0 : at + 1 );
    if ( open != std::string::npos && c.meetup_login[c.meetup_login.size() - 1] == ']' ) {
      c.meetup_login
        = c.meetup_login.substr( 0, open ) + c.meetup_login.substr( open + 1, c.meetup_login.size() - open - 2 );
    }
  }

  c.predict = s.value( "predict" );
  if ( s.flag( "a" ) ) {
    c.predict = "always";
  }
  if ( s.flag( "n" ) ) {
    c.predict = "never";
  }
  if ( c.predict.empty() ) {
    c.predict = prediction ? prediction : "adaptive";
  }
  if ( !one_of( c.predict, "adaptive always never experimental" ) ) {
    throw Failure( name + ": Unknown mode \"" + c.predict + "\".", true );
  }

  c.remote_ip = s.value( "experimental-remote-ip", "proxy" );
  if ( !one_of( c.remote_ip, "local remote proxy" ) ) {
    throw Failure( "Unknown parameter " + c.remote_ip );
  }

  c.family = lower( s.value( "family", "prefer-inet" ) );
  if ( s.flag( "4" ) ) {
    c.family = "inet";
  }
  if ( s.flag( "6" ) ) {
    c.family = "inet6";
  }
  if ( !one_of( c.family, "inet inet6 auto all prefer-inet prefer-inet6" ) ) {
    throw Failure( name + ": Invalid family " + c.family );
  }

  if ( s.has( "port" ) ) {
    check_port_request( s.value( "port" ), name );
  }
  return c;
}

void Gosh::Command::check_port_request( const std::string& request, const std::string& name )
{
  const size_t colon = request.find( ':' );
  const std::string low = request.substr( 0, colon );
  const std::string high = colon == std::string::npos ? "" : request.substr( colon + 1 );
  if ( !all_digits( low ) || low.size() > 6 || ( colon != std::string::npos && ( !all_digits( high ) || high.size() > 6 ) ) ) {
    throw Failure( name + ": Server-side port or range (" + request + ") is not valid." );
  }
  const long l = atol( low.c_str() ), h = atol( high.c_str() );
  if ( l > 65535 ) {
    throw Failure( name + ": Server-side (low) port (" + low + ") must be within valid range [0..65535]." );
  }
  if ( colon != std::string::npos ) {
    if ( h <= 0 || h > 65535 ) {
      throw Failure( name + ": Server-side high port (" + high + ") must be within valid range [1..65535]." );
    }
    if ( l == 0 ) {
      throw Failure( name + ": Server-side port ranges may not be used with starting port 0 (" + request + ")." );
    }
    if ( l > h ) {
      throw Failure( name + ": Server-side port range (" + request + "): low port greater than high port." );
    }
  }
}
