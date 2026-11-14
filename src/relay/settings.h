/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING).

    The text of the functions is the fork's (mosh-1.4.0-pf.7, src/network/relay.cc
    and relay.h), which is Copyright 2012 Keith Winstein and the fork's authors,
    under the same license.
*/

/*
 * relay, settings: what the relays are told through the environment, read once into one value
 * (REQ1/PATH-1, REQ1/PATH-7; design v2, 2.2 "options"). Logic: text in, a value out; relay.cc
 * is the one place that calls getenv, and hands the text here.
 *
 * Each input, absent or wrong: an idle time that is missing, not a number, or under a second is
 * the default (a minute); any word but "never" leaves path-finding on; with no meetup and
 * nothing said of STUN servers, two public ones are asked; "none" names no server.
 */

#ifndef GOSH_RELAY_SETTINGS_H
#define GOSH_RELAY_SETTINGS_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "src/relay/paths.h"

namespace Gosh {

/* the plain STUN servers asked where we appear to be from outside, unless there is a meetup, or MOSH_STUN says otherwise */
static const char RELAY_DEFAULT_STUN[] = "stun.l.google.com:19302,stun.cloudflare.com:3478";

/* A list separated by commas; empty items and "none" name nothing. */
inline std::vector<std::string> comma_list( const std::string& text )
{
  std::vector<std::string> ret;
  std::string item;
  for ( size_t i = 0; i <= text.size(); i++ ) {
    if ( i == text.size() || text[i] == ',' ) {
      if ( !item.empty() && item != "none" ) {
        ret.push_back( item );
      }
      item.clear();
    } else {
      item.push_back( text[i] );
    }
  }
  return ret;
}

struct RelaySettings
{
  uint64_t stream_idle;         /* how long the stream may go unneeded before it is let go (ms) */
  bool path_finding;            /* look for a way straight through to the other relay */
  std::vector<std::string> ask; /* meetups, then STUN servers: whom to ask where we appear to be */
  std::string meetup_key;       /* for a meetup, which answers only those who have it */
};

/*
 * The local relay's settings. Each text is the environment variable's value, or NULL if it is not set:
 *   idle    MOSH_RELAY_STREAM_IDLE_MS  (for tests, which cannot wait a minute to see the stream let go)
 *   punch   MOSH_PUNCH                 "never": no path-finding
 *   meetup  MOSH_MEETUP                our own meetups
 *   stun    MOSH_STUN                  plain STUN servers; or, when nothing at all is named, two public ones
 *   key     MOSH_MEETUP_KEY
 * stream_only: told to use the stream and nothing else, so no path-finding either.
 */
inline RelaySettings relay_settings( bool stream_only,
                                     const char* idle,
                                     const char* punch,
                                     const char* meetup,
                                     const char* stun,
                                     const char* key )
{
  RelaySettings settings;
  settings.stream_idle = ( idle && atol( idle ) >= 1000 ) ? atol( idle ) : RelayRules::STREAM_IDLE_MS;
  settings.path_finding = !stream_only && !( punch && strcmp( punch, "never" ) == 0 );
  settings.ask = comma_list( meetup ? meetup : "" );
  const std::vector<std::string> plain = comma_list( stun ? stun : ( settings.ask.empty() ? RELAY_DEFAULT_STUN : "" ) );
  settings.ask.insert( settings.ask.end(), plain.begin(), plain.end() );
  settings.meetup_key = key ? key : "";
  return settings;
}

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
