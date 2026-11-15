/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/PATH-1, REQ1/PATH-7: what the relays are told through the environment (settings.h). */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/relay/settings.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-88s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

typedef std::vector<std::string> Words;

int main()
{
  using Gosh::relay_settings;
  Gosh::RelaySettings s = relay_settings( false, NULL, NULL, NULL, NULL, NULL );
  check( "told nothing: the stream is let go after a minute, path-finding is on, no key",
         s.stream_idle == 60000 && s.path_finding && s.meetup_key.empty() );
  check( "and no STUN server is asked: no third party unless named (REQ1/PATH-7; v15 scope decisions)",
         s.ask.empty() );

  check( "an idle time of 3000: three seconds", relay_settings( false, "3000", NULL, NULL, NULL, NULL ).stream_idle == 3000 );
  check( "of 1000: one second", relay_settings( false, "1000", NULL, NULL, NULL, NULL ).stream_idle == 1000 );
  check( "of 300000: five minutes", relay_settings( false, "300000", NULL, NULL, NULL, NULL ).stream_idle == 300000 );
  check( "of a negative number: the minute", relay_settings( false, "-5000", NULL, NULL, NULL, NULL ).stream_idle == 60000 );
  check( "of 999, of nothing, of a word: the minute",
         relay_settings( false, "999", NULL, NULL, NULL, NULL ).stream_idle == 60000
           && relay_settings( false, "", NULL, NULL, NULL, NULL ).stream_idle == 60000
           && relay_settings( false, "soon", NULL, NULL, NULL, NULL ).stream_idle == 60000 );

  check( "MOSH_PUNCH=never: no path-finding", !relay_settings( false, NULL, "never", NULL, NULL, NULL ).path_finding );
  check( "a word that only begins like it: path-finding", relay_settings( false, NULL, "no", NULL, NULL, NULL ).path_finding
                                                            && relay_settings( false, NULL, "nevermore", NULL, NULL, NULL ).path_finding );
  check( "a key with no meetup named is kept all the same", relay_settings( false, NULL, NULL, NULL, NULL, "k" ).meetup_key == "k" );
  check( "any other word: path-finding", relay_settings( false, NULL, "always", NULL, NULL, NULL ).path_finding
                                           && relay_settings( false, NULL, "", NULL, NULL, NULL ).path_finding );
  check( "the stream and nothing else: no path-finding, whatever the word",
         !relay_settings( true, NULL, "always", NULL, NULL, NULL ).path_finding );

  s = relay_settings( false, NULL, NULL, "meet.example:3478,other.example:1", NULL, "k3y" );
  check( "meetups named, nothing said of STUN: the meetups alone, and their key",
         s.ask == Words( { "meet.example:3478", "other.example:1" } ) && s.meetup_key == "k3y" );
  s = relay_settings( false, NULL, NULL, "meet.example:3478", "stun.example:19302", NULL );
  check( "meetups and STUN servers named: the meetups first", s.ask == Words( { "meet.example:3478", "stun.example:19302" } ) );
  check( "MOSH_STUN=none, no meetup: nobody on the Internet is asked", relay_settings( false, NULL, NULL, NULL, "none", NULL ).ask.empty() );
  check( "MOSH_STUN empty: likewise", relay_settings( false, NULL, NULL, NULL, "", NULL ).ask.empty() );
  check( "empty items and \"none\" in a list name nothing", Gosh::comma_list( ",a,,none,b," ) == Words( { "a", "b" } ) );

  std::string address = "unset";
  check( "what ssh says of a connection: the client's address is its first word",
         Gosh::ssh_client_address( "192.0.2.7 51000 198.51.100.9 22" ) == "192.0.2.7" );
  check( "and the server's its third", Gosh::ssh_server_address( "192.0.2.7 51000 198.51.100.9 22", address ) && address == "198.51.100.9" );
  address = "unset";
  check( "with fewer than four words there is no server's address",
         !Gosh::ssh_server_address( "192.0.2.7 51000 198.51.100.9", address ) && !Gosh::ssh_server_address( "", address )
           && address == "unset" );
  check( "one word alone is taken for the client's address", Gosh::ssh_client_address( "2001:db8::7" ) == "2001:db8::7" );

  if ( failures ) {
    printf( "FAIL REQ1/PATH-1, REQ1/PATH-7: %d checks of the relay's settings failed\n", failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/PATH-1, REQ1/PATH-7: the relay's settings\n" );
  return EXIT_SUCCESS;
}
