/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING). gosh carries mosh's code
    and is not the mosh project's work.
*/

#include "src/seam/options.h"

namespace Gosh {

bool take_forward_options( const std::vector<std::string>& args,
                           const std::string& core_values,
                           bool allow_headless,
                           ForwardOptions& options,
                           std::vector<std::string>& rest,
                           std::string& error )
{
  options = ForwardOptions();
  rest.clear();
  error.clear();

  size_t i = 0;
  for ( ; i < args.size(); i++ ) {
    const std::string& word = args[i];
    if ( word == "--" || word.size() < 2 || word[0] != '-' ) {
      break; /* the options are over; this word and the rest are the core's */
    }

    std::string kept = "-"; /* the letters of this word that are the core's */
    for ( size_t at = 1; at < word.size(); at++ ) {
      const char letter = word[at];

      if ( letter == 'L' || letter == 'R' || letter == 'F' ) {
        /* one of gosh's that takes a value: the rest of this word, or the next word */
        std::string value = word.substr( at + 1 );
        if ( value.empty() ) {
          if ( i + 1 >= args.size() ) {
            error = std::string( "-" ) + letter + " needs a value";
            return false;
          }
          value = args[++i];
        }
        if ( letter == 'F' ) {
          options.parity = value;
        } else {
          const bool udp = value == "U";
          if ( udp ) {
            if ( i + 1 >= args.size() ) {
              error = std::string( "-" ) + letter + "U needs a forwarding specification";
              return false;
            }
            value = args[++i];
          }
          if ( letter == 'L' ) {
            ( udp ? options.udp : options.tcp ).push_back( value );
          } else {
            ( udp ? options.udp_remote : options.tcp_remote ).push_back( value );
          }
        }
        break; /* the value used up the rest of the word */
      }

      if ( letter == 'N' && allow_headless ) {
        options.headless = true;
        continue;
      }

      kept.push_back( letter );
      if ( core_values.find( letter ) != std::string::npos ) {
        /* one of the core's that takes a value: keep it with its value, unread */
        kept += word.substr( at + 1 );
        if ( at + 1 >= word.size() && i + 1 < args.size() ) {
          rest.push_back( kept );
          kept.clear();
          rest.push_back( args[++i] );
        }
        break;
      }
    }
    if ( kept.size() > 1 ) {
      rest.push_back( kept );
    }
  }

  for ( ; i < args.size(); i++ ) {
    rest.push_back( args[i] );
  }
  return true;
}

static Role role_named( const std::string& name )
{
  return name == "gosh" ? THE_COMMAND : name == "gocp" ? THE_COPY : THE_CLIENT;
}

Role which_role( const std::string& program, const std::vector<std::string>& args, std::string& called, size_t& skip )
{
  called = program;
  const size_t slash = called.find_last_of( "/\\" );
  if ( slash != std::string::npos ) {
    called.erase( 0, slash + 1 );
  }
  if ( called.size() > 4 && called.compare( called.size() - 4, 4, ".exe" ) == 0 ) {
    called.erase( called.size() - 4 );
  }

  skip = 0;
  Role role = role_named( called );
  if ( role == THE_CLIENT && !args.empty() && role_named( args[0] ) != THE_CLIENT ) {
    called = args[0];
    role = role_named( called );
    skip = 1;
  }
  return role;
}

}
