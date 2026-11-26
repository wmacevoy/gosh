/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The text of the functions is the fork's (mosh-1.4.0-pf.7, src/network/relay.cc and relay.h). */

/*
 * relay, settings: what the relays are told through the environment, read once into one value
 * (REQ1/PATH-1, REQ1/PATH-7; design v2, 2.2 "options"). Logic: text in, a value out; local.cc and remote.cc
 * are the places that call getenv, and hand the text here.
 *
 * Each input, absent or wrong: an idle time that is missing, not a number, or under a second is
 * the default (a minute); any word but "never" leaves path-finding on; with no meetup and
 * nothing said of STUN servers, none is asked (no third party unless named: v15's scope
 * decisions); "none" names no server.
 */

#ifndef GOSH_RELAY_SETTINGS_H
#define GOSH_RELAY_SETTINGS_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "src/relay/imp/paths.h"

namespace Gosh::Relay::Imp {

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
  std::string meetup;           /* the meetup to use, "host:port": the first named; empty if none */
  std::vector<std::string> ticket_command; /* how to get a ticket for it, over ssh; empty if there is no meetup */
};

/* Words given as one text, separated by the character 0x1F, as the command gives them (src/command). */
inline std::vector<std::string> unit_separated( const std::string& text )
{
  std::vector<std::string> words;
  std::string word;
  for ( size_t i = 0; i <= text.size(); i++ ) {
    if ( i == text.size() || text[i] == '\x1f' ) {
      words.push_back( word );
      word.clear();
    } else {
      word.push_back( text[i] );
    }
  }
  return words;
}

/*
 * The local relay's settings. Each text is the environment variable's value, or NULL if it is not set:
 *   idle    GOSH_RELAY_STREAM_IDLE_MS  (for tests, which cannot wait a minute to see the stream let go)
 *   punch   GOSH_PUNCH                 "never": no path-finding
 *   meetup  GOSH_MEETUP                our own meetups
 *   stun    GOSH_STUN                  plain STUN servers, asked only if named here
 *   ticket  GOSH_MEETUP_TICKET_COMMAND the words of the command that prints a ticket for the meetup
 *                                      (ssh to it, and "gosh-server meetup-ticket"), separated by 0x1F
 * stream_only: told to use the stream and nothing else, so no path-finding either.
 */
inline RelaySettings relay_settings( bool stream_only,
                                     const char* idle,
                                     const char* punch,
                                     const char* meetup,
                                     const char* stun,
                                     const char* ticket )
{
  RelaySettings settings;
  settings.stream_idle = ( idle && atol( idle ) >= 1000 ) ? atol( idle ) : RelayRules::STREAM_IDLE_MS;
  settings.path_finding = !stream_only && !( punch && strcmp( punch, "never" ) == 0 );
  /* "none" asks nobody on the Internet anything, a meetup included (REQ1/PATH-7, REQ15/PATH-4) */
  if ( stun && strcmp( stun, "none" ) == 0 ) {
    return settings;
  }
  const std::vector<std::string> meetups = comma_list( meetup ? meetup : "" );
  settings.ask = meetups;
  const std::vector<std::string> plain = comma_list( stun ? stun : "" ); /* (no third party unless named) */
  settings.ask.insert( settings.ask.end(), plain.begin(), plain.end() );
  /* (a plain STUN server is asked where we appear to be, and is never a meetup) */
  settings.meetup = meetups.empty() ? std::string() : meetups[0];
  if ( !settings.meetup.empty() && ticket && *ticket ) {
    settings.ticket_command = unit_separated( ticket );
  }
  return settings;
}

}

#endif
