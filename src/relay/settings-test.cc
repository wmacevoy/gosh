/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/PATH-1, REQ1/PATH-7: what the relays are told through the environment (settings.h). */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/relay/settings.h"
#include "vendor/facts/facts.h"



typedef std::vector<std::string> Words;
/* REQ1/PATH-1, REQ1/PATH-7: the relay's settings */
FACTS( Settings )
{
  using Gosh::relay_settings;
  Gosh::RelaySettings s = relay_settings( false, NULL, NULL, NULL, NULL, NULL );
  FACT_CHECK( ( s.stream_idle == 60000 && s.path_finding && s.meetup.empty() && s.ticket_command.empty() ), "told nothing: the stream is let go after a minute, path-finding is on, no meetup" );
  FACT_CHECK( s.ask.empty(), "and no STUN server is asked: no third party unless named (REQ1/PATH-7; v15 scope decisions)" );

  FACT_CHECK( relay_settings( false, "3000", NULL, NULL, NULL, NULL ).stream_idle == 3000, "an idle time of 3000: three seconds" );
  FACT_CHECK( relay_settings( false, "1000", NULL, NULL, NULL, NULL ).stream_idle == 1000, "of 1000: one second" );
  FACT_CHECK( relay_settings( false, "300000", NULL, NULL, NULL, NULL ).stream_idle == 300000, "of 300000: five minutes" );
  FACT_CHECK( relay_settings( false, "-5000", NULL, NULL, NULL, NULL ).stream_idle == 60000, "of a negative number: the minute" );
  FACT_CHECK( ( relay_settings( false, "999", NULL, NULL, NULL, NULL ).stream_idle == 60000
           && relay_settings( false, "", NULL, NULL, NULL, NULL ).stream_idle == 60000
           && relay_settings( false, "soon", NULL, NULL, NULL, NULL ).stream_idle == 60000 ), "of 999, of nothing, of a word: the minute" );

  FACT_CHECK( !relay_settings( false, NULL, "never", NULL, NULL, NULL ).path_finding, "GOSH_PUNCH=never: no path-finding" );
  FACT_CHECK( ( relay_settings( false, NULL, "no", NULL, NULL, NULL ).path_finding
                                                            && relay_settings( false, NULL, "nevermore", NULL, NULL, NULL ).path_finding ), "a word that only begins like it: path-finding" );
  FACT_CHECK( relay_settings( false, NULL, NULL, NULL, NULL, "ssh\x1fhost" ).ticket_command.empty(), "a ticket's command with no meetup named is no use, and not kept" );
  FACT_CHECK( ( relay_settings( false, NULL, "always", NULL, NULL, NULL ).path_finding
                                           && relay_settings( false, NULL, "", NULL, NULL, NULL ).path_finding ), "any other word: path-finding" );
  FACT_CHECK( !relay_settings( true, NULL, "always", NULL, NULL, NULL ).path_finding, "the stream and nothing else: no path-finding, whatever the word" );

  s = relay_settings( false, NULL, NULL, "meet.example:3478,other.example:1", NULL, "ssh\x1f-T\x1fmeet.example\x1f--\x1fgosh-server meetup-ticket" );
  FACT_CHECK( ( s.ask == Words( { "meet.example:3478", "other.example:1" } ) ), "meetups named, nothing said of STUN: the meetups alone are asked" );
  FACT_CHECK( ( s.meetup == "meet.example:3478"
           && s.ticket_command == Words( { "ssh", "-T", "meet.example", "--", "gosh-server meetup-ticket" } ) ), "the first is the one used (REQ15/PATH-4), with its ticket's command in its words" );
  s = relay_settings( false, NULL, NULL, "meet.example:3478", "stun.example:19302", NULL );
  FACT_CHECK( ( s.ask == Words( { "meet.example:3478", "stun.example:19302" } ) ), "meetups and STUN servers named: the meetups first" );
  FACT_CHECK( relay_settings( false, NULL, NULL, NULL, "none", NULL ).ask.empty(), "GOSH_STUN=none, no meetup: nobody on the Internet is asked" );
  FACT_CHECK( relay_settings( false, NULL, NULL, NULL, "", NULL ).ask.empty(), "GOSH_STUN empty: likewise" );
  s = relay_settings( false, NULL, NULL, "meet.example:3478", "none", "ssh\x1fmeet.example" );
  FACT_CHECK( ( s.ask.empty() && s.meetup.empty() && s.ticket_command.empty() ), "GOSH_STUN=none with a meetup named: the meetup is not asked or used either (REQ15/PATH-4, REQ1/PATH-7)" );
  s = relay_settings( false, NULL, NULL, NULL, "stun.example:19302", "ssh\x1fhost" );
  FACT_CHECK( ( s.ask == Words( { "stun.example:19302" } )
                                                                           && s.meetup.empty() && s.ticket_command.empty() ), "a plain STUN server is asked, and never taken for a meetup" );
  FACT_CHECK( ( Gosh::comma_list( ",a,,none,b," ) == Words( { "a", "b" } ) ), "empty items and \"none\" in a list name nothing" );

  std::string address = "unset";
  FACT_CHECK( Gosh::ssh_client_address( "192.0.2.7 51000 198.51.100.9 22" ) == "192.0.2.7", "what ssh says of a connection: the client's address is its first word" );
  FACT_CHECK( ( Gosh::ssh_server_address( "192.0.2.7 51000 198.51.100.9 22", address ) && address == "198.51.100.9" ), "and the server's its third" );
  address = "unset";
  FACT_CHECK( ( !Gosh::ssh_server_address( "192.0.2.7 51000 198.51.100.9", address ) && !Gosh::ssh_server_address( "", address )
           && address == "unset" ), "with fewer than four words there is no server's address" );
  FACT_CHECK( Gosh::ssh_client_address( "2001:db8::7" ) == "2001:db8::7", "one word alone is taken for the client's address" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Settings );
}

FACTS_MAIN
