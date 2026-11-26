/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * ssh, a connection: what ssh says of the connection it made, as the remote end reads it. Logic:
 * text in, text out. (From the relay's settings.h, 2026-11-26.)
 */

#ifndef GOSH_SSH_CONNECTION_H
#define GOSH_SSH_CONNECTION_H

#include <string>

namespace Gosh::Ssh {

/*
 * From what ssh says of a connection (SSH_CONNECTION: "client-address client-port
 * server-address server-port"): the client's address, which is everything before the first
 * space; and the server's, which is there only if all four words are.
 */
inline std::string ssh_client_address( const std::string& ssh_connection )
{
  return ssh_connection.substr( 0, ssh_connection.find( ' ' ) );
}

inline bool ssh_server_address( const std::string& s, std::string& address )
{
  const size_t first = s.find( ' ' );
  const size_t second = first == std::string::npos ? first : s.find( ' ', first + 1 );
  const size_t third = second == std::string::npos ? second : s.find( ' ', second + 1 );
  if ( third == std::string::npos ) {
    return false;
  }
  address = s.substr( second + 1, third - second - 1 );
  return true;
}

}

#endif
