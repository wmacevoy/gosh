/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#include "src/include/config.h"

#include <cstdlib>
#include <vector>

#include "src/forwarding/forward.h"
#include "src/network/network.h"

using namespace Gosh;

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
