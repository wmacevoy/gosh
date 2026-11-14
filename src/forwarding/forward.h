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

/*
 * (gosh) forward: the specification of one forward, [bind_address:]port:host:hostport (REQ1/FWD-1, REQ1/FWD-4).
 * The fork's text at mosh-1.4.0-pf.7, from src/network/udpforward.{h,cc}, moved here
 * under its own name and into gosh's namespace; nothing else changed. Logic: no socket, no clock.
 */

#ifndef GOSH_FORWARD_H
#define GOSH_FORWARD_H

#include <string>

namespace Gosh {

/* [bind_address:]port:host:hostport -- IPv6 addresses go in [brackets]. */
class ForwardSpec
{
public:
  std::string bind_host; /* empty or "*" means all interfaces; default is loopback */
  std::string bind_port;
  std::string target_host;
  std::string target_port;

  ForwardSpec( const std::string& spec ); /* throws NetworkException */
};

}

#endif
