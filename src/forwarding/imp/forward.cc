/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/include/config.h"

#include <cstdlib>
#include <vector>

#include "src/forwarding/imp/forward.h"
#include "src/network/network.h"

using namespace Gosh;
using namespace Gosh::Forwarding;
using namespace Gosh::Forwarding::Imp;

static bool valid_port( const std::string& s )
{
  if ( s.empty() || s.size() > 5 || s.find_first_not_of( "0123456789" ) != std::string::npos ) {
    return false;
  }
  long value = strtol( s.c_str(), NULL, 10 );
  return value >= 1 && value <= 65535;
}

ForwardSpec::ForwardSpec( const std::string& spec )
  : bind_host( "127.0.0.1" ), bind_port(), target_host(), target_port()
{
  /* split at colons that are not inside [brackets] */
  std::vector<std::string> fields( 1 );
  bool bracketed = false;
  for ( std::string::const_iterator i = spec.begin(); i != spec.end(); i++ ) {
    if ( *i == '[' && !bracketed && fields.back().empty() ) {
      bracketed = true;
    } else if ( *i == ']' && bracketed ) {
      bracketed = false;
    } else if ( *i == ':' && !bracketed ) {
      fields.push_back( std::string() );
    } else {
      fields.back().push_back( *i );
    }
  }

  const std::string usage = "Bad forwarding specification (" + spec + "): ";

  if ( bracketed || ( fields.size() != 3 && fields.size() != 4 ) ) {
    throw Network::NetworkException( usage + "expected [bind_address:]port:host:hostport", 0 );
  }

  if ( fields.size() == 4 ) {
    bind_host = fields[0];
    fields.erase( fields.begin() );
  }
  bind_port = fields[0];
  target_host = fields[1];
  target_port = fields[2];

  if ( !valid_port( bind_port ) || !valid_port( target_port ) ) {
    throw Network::NetworkException( usage + "ports must be numbers from 1 to 65535", 0 );
  }
  if ( target_host.empty() ) {
    throw Network::NetworkException( usage + "missing host", 0 );
  }
}
