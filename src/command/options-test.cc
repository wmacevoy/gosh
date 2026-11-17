/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CMD-1: the command's words read into settings, and what is refused (options.h); with
 * REQ1/CP-7 (--stdio reads in order), REQ1/PATH-7 (--punch, --stun for the relays), REQ15/PATH-4
 * and REQ17/PATH-8 (--meetup, with an IPv6 address). Plain strings; no process, no environment.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/command/options.h"
#include "vendor/facts/facts.h"

using namespace Gosh::Command;
typedef std::vector<std::string> Words;



/* The settings read from the words, or the failure's message (prefixed "!", and "+" if with usage). */
static std::string parsed( const Words& words, bool in_order, Settings& s )
{
  s = Settings();
  try {
    parse_options( words, in_order, s );
  } catch ( const Failure& f ) {
    return std::string( "!" ) + ( f.with_usage ? "+" : "" ) + f.message;
  }
  return "";
}

static std::string refused( const Words& words, const char* prediction = NULL )
{
  Settings s;
  const std::string p = parsed( words, false, s );
  if ( !p.empty() ) {
    return p;
  }
  try {
    choose( s, "gosh", prediction );
  } catch ( const Failure& f ) {
    return std::string( "!" ) + ( f.with_usage ? "+" : "" ) + f.message;
  }
  return "";
}
/* REQ1/CMD-1, REQ1/CP-7, REQ1/PATH-7, REQ15/PATH-4, REQ17/PATH-8: the command's options */
FACTS( Options )
{
  Settings s;

  /* ---- reading the words */
  FACT_CHECK( ( parsed( { "--ssh=ssh -p 2", "-p", "60001", "-N", "host" }, false, s ).empty() && s.value( "ssh" ) == "ssh -p 2"
           && s.value( "port" ) == "60001" && s.flag( "N" ) && s.rest == Words( { "host" } ) ), "a value after = and as the next word; a flag; the host left over" );
  FACT_CHECK( ( parsed( { "--fam=inet", "--exp=local", "h" }, false, s ).empty() && s.value( "family" ) == "inet"
           && s.value( "experimental-remote-ip" ) == "local" ), "a long name cut short, while it names one option only" );
  FACT_CHECK( parsed( { "--pr=1", "h" }, false, s ) == "!+Unknown option: pr", "cut short to where it names several: refused, with the usage" );
  FACT_CHECK( ( parsed( { "--p=1", "h" }, false, s ).empty() && s.value( "port" ) == "1" ), "a name that is an option's own short name is that option, not a cut" );
  FACT_CHECK( parsed( { "h", "top", "-d" }, false, s ) == "!+Unknown option: d", "a word after the host that looks like an option is one" );
  FACT_CHECK( parsed( { "-f", "h" }, false, s ) == "!+Unknown option: f", "a one-letter name is not a short form of a long one" );
  FACT_CHECK( parsed( { "--frobnicate", "h" }, false, s ) == "!+Unknown option: frobnicate", "an unknown option: refused, with the usage" );
  FACT_CHECK( ( parsed( { "--no-init", "--nossh-pty", "h" }, false, s ).empty()
                                              && !s.flag( "init", true ) && !s.flag( "ssh-pty", true ) ), "a negatable flag, as no and no-" );
  FACT_CHECK( parsed( { "--no-local", "h" }, false, s ) == "!+Unknown option: no-local", "a flag that is not negatable cannot be" );
  FACT_CHECK( parsed( { "h", "--server" }, false, s ) == "!+Option server requires an argument", "a value missing at the end" );
  FACT_CHECK( parsed( { "--local=yes", "h" }, false, s ) == "!+Option local does not take an argument", "a value given to a flag" );
  FACT_CHECK( ( parsed( { "-L", "1:a:1", "--tcp-forward=2:b:2", "h" }, false, s ).empty()
           && s.all( "tcp-forward" ) == Words( { "1:a:1", "2:b:2" } ) && s.value( "tcp-forward" ) == "2:b:2" ), "repeated values are all kept, the last is the value" );
  FACT_CHECK( ( parsed( { "-LU", "5:a:5", "-RU", "6:b:6", "h" }, false, s ).empty() && s.value( "udp-forward" ) == "5:a:5"
           && s.value( "udp-remote-forward" ) == "6:b:6" && !s.has( "tcp-forward" ) ), "-LU and -RU are names of their own, not -L with a value" );
  FACT_CHECK( ( parsed( { "h", "-N", "--", "ls", "-l" }, false, s ).empty() && s.flag( "N" )
           && s.rest == Words( { "h", "ls", "-l" } ) ), "not in order: words after the host are still options; -- ends them, and is dropped" );

  /* ---- --stdio, in order (REQ1/CP-7) */
  FACT_CHECK( ( wants_stdio( { "--stdio", "h", "rsync" } ) && !wants_stdio( { "h", "--", "--stdio" } ) && !wants_stdio( { "h" } ) ), "--stdio before any -- is wanted; after one it is the command's" );
  FACT_CHECK( ( parsed( { "--stdio", "-l", "me", "h", "rsync", "--server", "-e" }, true, s ).empty() && s.value( "l" ) == "me"
           && s.rest == Words( { "h", "rsync", "--server", "-e" } ) ), "in order: the first word that is no option ends them, and all after it is the command's" );
  FACT_CHECK( ( parsed( { "--stdio", "h", "--", "cat" }, true, s ).empty() && s.rest == Words( { "h", "--", "cat" } ) ), "in order: a -- is kept where it stood" );

  /* ---- what is chosen, and what is refused */
  Settings plain;
  parse_options( { "h" }, false, plain );
  const Choices d = choose( plain, "gosh", NULL );
  FACT_CHECK( ( d.relay == "auto" && d.punch == "auto" && d.predict == "adaptive" && d.remote_ip == "proxy"
           && d.family == "prefer-inet" && d.fragment_parity.empty() && d.meetup_login.empty() && d.relays_read.empty() ), "the defaults: relay and punch auto, prediction adaptive, ip by proxy, family prefer-inet (REQ17/PATH-8: the session's first try is by IPv4)" );
  FACT_CHECK( choose( plain, "gosh", "never" ).predict == "never", "prediction from MOSH_PREDICTION_DISPLAY when no option says" );
  FACT_CHECK( refused( { "h" }, "" ) == "!+gosh: Unknown mode \"\".", "and refused when that is not a mode, even empty" );
  parse_options( { "-a", "--predict=never" }, false, s = Settings() );
  FACT_CHECK( choose( s, "gosh", "never" ).predict == "always", "-a over --predict" );
  parse_options( { "-n", "-a" }, false, s = Settings() );
  FACT_CHECK( choose( s, "gosh", NULL ).predict == "never", "-n over -a" );
  FACT_CHECK( refused( { "--predict=sometimes", "h" } ) == "!+gosh: Unknown mode \"sometimes\".", "an unknown prediction mode" );
  FACT_CHECK( refused( { "--relay=sometimes", "h" } ) == "!+gosh: Unknown relay mode \"sometimes\".", "an unknown relay mode" );
  FACT_CHECK( refused( { "--punch=always", "h" } ) == "!+gosh: Unknown punch mode \"always\".", "an unknown punch mode" );
  FACT_CHECK( refused( { "--fragment-parity=some", "h" } ) == "!+gosh: Unknown fragment parity mode \"some\".", "an unknown fragment parity mode" );
  parse_options( { "--fragment-parity=always", "--no-fragment-parity" }, false, s = Settings() );
  FACT_CHECK( choose( s, "gosh", NULL ).fragment_parity == "never", "--no-fragment-parity is never" );
  FACT_CHECK( refused( { "--experimental-remote-ip=dns", "h" } ) == "!Unknown parameter dns", "an unknown way to find the server's address" );
  FACT_CHECK( refused( { "--family=ipx", "h" } ) == "!gosh: Invalid family ipx", "an unknown family" );
  parse_options( { "--family=PREFER-INET6" }, false, s = Settings() );
  FACT_CHECK( choose( s, "gosh", NULL ).family == "prefer-inet6", "a family in capitals is taken in small letters" );
  parse_options( { "--family=all", "-4", "-6" }, false, s = Settings() );
  FACT_CHECK( choose( s, "gosh", NULL ).family == "inet6", "-6 over -4 over --family" );
  FACT_CHECK( refused( { "--relay=x", "--punch=y", "h" } ) == "!+gosh: Unknown relay mode \"x\".", "the first refusal is the one said: relay before punch" );

  /* ---- the relays' own variables (REQ1/PATH-7) */
  parse_options( { "--punch=never", "--stun=none" }, false, s = Settings() );
  const Choices r = choose( s, "gosh", NULL );
  FACT_CHECK( ( r.relays_read.size() == 2 && r.relays_read[0] == std::make_pair( std::string( "GOSH_PUNCH" ), std::string( "never" ) )
           && r.relays_read[1] == std::make_pair( std::string( "GOSH_STUN" ), std::string( "none" ) ) ), "--punch and --stun, when given, are told to the relays, in that order" );

  /* ---- the meetup (REQ15/PATH-4, REQ17/PATH-8) */
  parse_options( { "--meetup=me@meet.example:3478" }, false, s = Settings() );
  Choices m = choose( s, "gosh", NULL );
  FACT_CHECK( ( m.meetup_login == "me@meet.example" && m.relays_read.size() == 1
           && m.relays_read[0].first == "GOSH_MEETUP" && m.relays_read[0].second == "meet.example:3478" ), "--meetup: the relays get HOST:PORT, ssh gets USER@HOST" );
  parse_options( { "--meetup=me@[2001:db8::1]:3478" }, false, s = Settings() );
  m = choose( s, "gosh", NULL );
  FACT_CHECK( ( m.meetup_login == "me@2001:db8::1" && m.relays_read[0].second == "[2001:db8::1]:3478" ), "an IPv6 meetup: the relays get [ADDRESS]:PORT, ssh the address without its brackets" );
  parse_options( { "--meetup=[2001:db8::1]:3478" }, false, s = Settings() );
  FACT_CHECK( choose( s, "gosh", NULL ).meetup_login == "2001:db8::1", "and with no user" );
  parse_options( { "--meetup=me@meet.example:3478", "--stun=none" }, false, s = Settings() );
  m = choose( s, "gosh", NULL );
  FACT_CHECK( ( m.meetup_login.empty() && m.relays_read.size() == 1 && m.relays_read[0].first == "GOSH_STUN" ), "--stun=none keeps the session off the meetup: no ticket's login, no meetup for the relays (REQ15/PATH-4)" );
  FACT_CHECK( ( refused( { "--meetup=-oProxyCommand=touch x@h:22", "h" } ) == "!+gosh: --meetup needs HOST:PORT."
           && refused( { "--meetup=-h:22", "h" } ) == "!+gosh: --meetup needs HOST:PORT." ), "a meetup whose login begins with - is refused: ssh would take it for an option (the review of item 10)" );
  FACT_CHECK( refused( { "--meetup=meet.example", "h" } ) == "!+gosh: --meetup needs HOST:PORT.", "a meetup with no port is refused" );
  FACT_CHECK( refused( { "--meetup=:3478", "h" } ) == "!+gosh: --meetup needs HOST:PORT.", "nor with nothing before its port" );
  /* (REQ17/PATH-8; found by review, 2026-11-16: these were taken, and made an ssh login of "[2001:db8:") */
  FACT_CHECK( refused( { "--meetup=[2001:db8::1]", "h" } ) == "!+gosh: --meetup needs HOST:PORT.", "an IPv6 meetup with no port is refused" );
  FACT_CHECK( refused( { "--meetup=2001:db8::1", "h" } ) == "!+gosh: --meetup needs HOST:PORT.", "nor an IPv6 address without its brackets" );
  FACT_CHECK( refused( { "--meetup=me@[2001:db8::1]", "h" } ) == "!+gosh: --meetup needs HOST:PORT.", "nor with a user and no port" );
  FACT_CHECK( refused( { "--meetup=[2001:db8::1]:", "h" } ) == "!+gosh: --meetup needs HOST:PORT.", "nor with an empty port" );
  FACT_CHECK( ( refused( { "--meetup=meet.example:99999", "h" } ) == "!+gosh: --meetup needs HOST:PORT."
                                         && refused( { "--meetup=meet.example:x1", "h" } ) == "!+gosh: --meetup needs HOST:PORT." ), "nor a port that is not one" );

  /* ---- the server's port, or range */
  FACT_CHECK( ( refused( { "-p", "60001", "h" } ).empty() && refused( { "-p", "60001:60010", "h" } ).empty()
                                             && refused( { "-p", "0", "h" } ).empty() ), "a port, and a range, are taken" );
  FACT_CHECK( refused( { "-p", "x", "h" } ) == "!gosh: Server-side port or range (x) is not valid.", "a port that is not a number" );
  FACT_CHECK( refused( { "-p", "0000001", "h" } ) == "!gosh: Server-side port or range (0000001) is not valid.", "nor one of more than six digits" );
  FACT_CHECK( refused( { "-p", "65536", "h" } ) == "!gosh: Server-side (low) port (65536) must be within valid range [0..65535].", "a port over 65535" );
  FACT_CHECK( refused( { "-p", "1:0", "h" } ) == "!gosh: Server-side high port (0) must be within valid range [1..65535].", "a range's high port of 0" );
  FACT_CHECK( refused( { "-p", "0:5", "h" } ) == "!gosh: Server-side port ranges may not be used with starting port 0 (0:5).", "a range from port 0" );
  FACT_CHECK( refused( { "-p", "9:5", "h" } ) == "!gosh: Server-side port range (9:5): low port greater than high port.", "a range backwards" );

  /* ---- text */
  FACT_CHECK( ( split( "a::b", ':' ) == Words( { "a", "", "b" } ) ), "split keeps empty parts" );
  FACT_CHECK( strip_end( " a b \r\n" ) == " a b", "strip_end takes line ends and spaces from the end only" );
  FACT_CHECK( ( all_digits( "012" ) && !all_digits( "" ) && !all_digits( "1a" ) ), "all_digits: not empty, digits only" );
  FACT_CHECK( ( one_of( "never", "auto never" ) && !one_of( "neve", "auto never" ) ), "one_of: whole words only" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Options );
}

FACTS_MAIN
