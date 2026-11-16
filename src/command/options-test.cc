/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CMD-1: the command's words read into settings, and what is refused (options.h); with
 * REQ1/CP-7 (--stdio reads in order), REQ1/PATH-7 (--punch, --stun for the relays), REQ15/PATH-4
 * and REQ16/PATH-8 (--meetup, with an IPv6 address). Plain strings; no process, no environment.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/command/options.h"

using namespace Gosh::Command;
typedef std::vector<std::string> Words;

static int failures = 0;

static void check( const char* what, bool ok, const std::string& got = "" )
{
  printf( "%-84s %s%s\n", what, ok ? "ok" : "FAIL ", ok ? "" : got.c_str() );
  if ( !ok ) {
    failures++;
  }
}

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

int main()
{
  Settings s;

  /* ---- reading the words */
  check( "a value after = and as the next word; a flag; the host left over",
         parsed( { "--ssh=ssh -p 2", "-p", "60001", "-N", "host" }, false, s ).empty() && s.value( "ssh" ) == "ssh -p 2"
           && s.value( "port" ) == "60001" && s.flag( "N" ) && s.rest == Words( { "host" } ) );
  check( "a long name cut short, while it names one option only",
         parsed( { "--fam=inet", "--exp=local", "h" }, false, s ).empty() && s.value( "family" ) == "inet"
           && s.value( "experimental-remote-ip" ) == "local" );
  check( "cut short to where it names several: refused, with the usage",
         parsed( { "--pr=1", "h" }, false, s ) == "!+Unknown option: pr" );
  check( "a name that is an option's own short name is that option, not a cut",
         parsed( { "--p=1", "h" }, false, s ).empty() && s.value( "port" ) == "1" );
  check( "a word after the host that looks like an option is one",
         parsed( { "h", "top", "-d" }, false, s ) == "!+Unknown option: d" );
  check( "a one-letter name is not a short form of a long one",
         parsed( { "-f", "h" }, false, s ) == "!+Unknown option: f" );
  check( "an unknown option: refused, with the usage", parsed( { "--frobnicate", "h" }, false, s ) == "!+Unknown option: frobnicate" );
  check( "a negatable flag, as no and no-", parsed( { "--no-init", "--nossh-pty", "h" }, false, s ).empty()
                                              && !s.flag( "init", true ) && !s.flag( "ssh-pty", true ) );
  check( "a flag that is not negatable cannot be", parsed( { "--no-local", "h" }, false, s ) == "!+Unknown option: no-local" );
  check( "a value missing at the end", parsed( { "h", "--server" }, false, s ) == "!+Option server requires an argument" );
  check( "a value given to a flag", parsed( { "--local=yes", "h" }, false, s ) == "!+Option local does not take an argument" );
  check( "repeated values are all kept, the last is the value",
         parsed( { "-L", "1:a:1", "--tcp-forward=2:b:2", "h" }, false, s ).empty()
           && s.all( "tcp-forward" ) == Words( { "1:a:1", "2:b:2" } ) && s.value( "tcp-forward" ) == "2:b:2" );
  check( "-LU and -RU are names of their own, not -L with a value",
         parsed( { "-LU", "5:a:5", "-RU", "6:b:6", "h" }, false, s ).empty() && s.value( "udp-forward" ) == "5:a:5"
           && s.value( "udp-remote-forward" ) == "6:b:6" && !s.has( "tcp-forward" ) );
  check( "not in order: words after the host are still options; -- ends them, and is dropped",
         parsed( { "h", "-N", "--", "ls", "-l" }, false, s ).empty() && s.flag( "N" )
           && s.rest == Words( { "h", "ls", "-l" } ) );

  /* ---- --stdio, in order (REQ1/CP-7) */
  check( "--stdio before any -- is wanted; after one it is the command's",
         wants_stdio( { "--stdio", "h", "rsync" } ) && !wants_stdio( { "h", "--", "--stdio" } ) && !wants_stdio( { "h" } ) );
  check( "in order: the first word that is no option ends them, and all after it is the command's",
         parsed( { "--stdio", "-l", "me", "h", "rsync", "--server", "-e" }, true, s ).empty() && s.value( "l" ) == "me"
           && s.rest == Words( { "h", "rsync", "--server", "-e" } ) );
  check( "in order: a -- is kept where it stood",
         parsed( { "--stdio", "h", "--", "cat" }, true, s ).empty() && s.rest == Words( { "h", "--", "cat" } ) );

  /* ---- what is chosen, and what is refused */
  Settings plain;
  parse_options( { "h" }, false, plain );
  const Choices d = choose( plain, "gosh", NULL );
  check( "the defaults: relay and punch auto, prediction adaptive, ip by proxy, family prefer-inet",
         d.relay == "auto" && d.punch == "auto" && d.predict == "adaptive" && d.remote_ip == "proxy"
           && d.family == "prefer-inet" && d.fragment_parity.empty() && d.meetup_login.empty() && d.relays_read.empty() );
  check( "prediction from MOSH_PREDICTION_DISPLAY when no option says", choose( plain, "gosh", "never" ).predict == "never" );
  check( "and refused when that is not a mode, even empty", refused( { "h" }, "" ) == "!+gosh: Unknown mode \"\"." );
  parse_options( { "-a", "--predict=never" }, false, s = Settings() );
  check( "-a over --predict", choose( s, "gosh", "never" ).predict == "always" );
  parse_options( { "-n", "-a" }, false, s = Settings() );
  check( "-n over -a", choose( s, "gosh", NULL ).predict == "never" );
  check( "an unknown prediction mode", refused( { "--predict=sometimes", "h" } ) == "!+gosh: Unknown mode \"sometimes\"." );
  check( "an unknown relay mode", refused( { "--relay=sometimes", "h" } ) == "!+gosh: Unknown relay mode \"sometimes\"." );
  check( "an unknown punch mode", refused( { "--punch=always", "h" } ) == "!+gosh: Unknown punch mode \"always\"." );
  check( "an unknown fragment parity mode",
         refused( { "--fragment-parity=some", "h" } ) == "!+gosh: Unknown fragment parity mode \"some\"." );
  parse_options( { "--fragment-parity=always", "--no-fragment-parity" }, false, s = Settings() );
  check( "--no-fragment-parity is never", choose( s, "gosh", NULL ).fragment_parity == "never" );
  check( "an unknown way to find the server's address", refused( { "--experimental-remote-ip=dns", "h" } ) == "!Unknown parameter dns" );
  check( "an unknown family", refused( { "--family=ipx", "h" } ) == "!gosh: Invalid family ipx" );
  parse_options( { "--family=PREFER-INET6" }, false, s = Settings() );
  check( "a family in capitals is taken in small letters", choose( s, "gosh", NULL ).family == "prefer-inet6" );
  parse_options( { "--family=all", "-4", "-6" }, false, s = Settings() );
  check( "-6 over -4 over --family", choose( s, "gosh", NULL ).family == "inet6" );
  check( "the first refusal is the one said: relay before punch",
         refused( { "--relay=x", "--punch=y", "h" } ) == "!+gosh: Unknown relay mode \"x\"." );

  /* ---- the relays' own variables (REQ1/PATH-7) */
  parse_options( { "--punch=never", "--stun=none" }, false, s = Settings() );
  const Choices r = choose( s, "gosh", NULL );
  check( "--punch and --stun, when given, are told to the relays, in that order",
         r.relays_read.size() == 2 && r.relays_read[0] == std::make_pair( std::string( "GOSH_PUNCH" ), std::string( "never" ) )
           && r.relays_read[1] == std::make_pair( std::string( "GOSH_STUN" ), std::string( "none" ) ) );

  /* ---- the meetup (REQ15/PATH-4, REQ16/PATH-8) */
  parse_options( { "--meetup=me@meet.example:3478" }, false, s = Settings() );
  Choices m = choose( s, "gosh", NULL );
  check( "--meetup: the relays get HOST:PORT, ssh gets USER@HOST",
         m.meetup_login == "me@meet.example" && m.relays_read.size() == 1
           && m.relays_read[0].first == "GOSH_MEETUP" && m.relays_read[0].second == "meet.example:3478" );
  parse_options( { "--meetup=me@[2001:db8::1]:3478" }, false, s = Settings() );
  m = choose( s, "gosh", NULL );
  check( "an IPv6 meetup: the relays get [ADDRESS]:PORT, ssh the address without its brackets",
         m.meetup_login == "me@2001:db8::1" && m.relays_read[0].second == "[2001:db8::1]:3478" );
  parse_options( { "--meetup=[2001:db8::1]:3478" }, false, s = Settings() );
  check( "and with no user", choose( s, "gosh", NULL ).meetup_login == "2001:db8::1" );
  parse_options( { "--meetup=me@meet.example:3478", "--stun=none" }, false, s = Settings() );
  m = choose( s, "gosh", NULL );
  check( "--stun=none keeps the session off the meetup: no ticket's login, no meetup for the relays (REQ15/PATH-4)",
         m.meetup_login.empty() && m.relays_read.size() == 1 && m.relays_read[0].first == "GOSH_STUN" );
  check( "a meetup with no port is refused", refused( { "--meetup=meet.example", "h" } ) == "!+gosh: --meetup needs HOST:PORT." );
  check( "nor with nothing before its port", refused( { "--meetup=:3478", "h" } ) == "!+gosh: --meetup needs HOST:PORT." );
  /* (REQ16/PATH-8; found by review, 2026-11-16: these were taken, and made an ssh login of "[2001:db8:") */
  check( "an IPv6 meetup with no port is refused", refused( { "--meetup=[2001:db8::1]", "h" } ) == "!+gosh: --meetup needs HOST:PORT." );
  check( "nor an IPv6 address without its brackets", refused( { "--meetup=2001:db8::1", "h" } ) == "!+gosh: --meetup needs HOST:PORT." );
  check( "nor with a user and no port", refused( { "--meetup=me@[2001:db8::1]", "h" } ) == "!+gosh: --meetup needs HOST:PORT." );
  check( "nor with an empty port", refused( { "--meetup=[2001:db8::1]:", "h" } ) == "!+gosh: --meetup needs HOST:PORT." );
  check( "nor a port that is not one", refused( { "--meetup=meet.example:99999", "h" } ) == "!+gosh: --meetup needs HOST:PORT."
                                         && refused( { "--meetup=meet.example:x1", "h" } ) == "!+gosh: --meetup needs HOST:PORT." );

  /* ---- the server's port, or range */
  check( "a port, and a range, are taken", refused( { "-p", "60001", "h" } ).empty() && refused( { "-p", "60001:60010", "h" } ).empty()
                                             && refused( { "-p", "0", "h" } ).empty() );
  check( "a port that is not a number", refused( { "-p", "x", "h" } ) == "!gosh: Server-side port or range (x) is not valid." );
  check( "nor one of more than six digits", refused( { "-p", "0000001", "h" } ) == "!gosh: Server-side port or range (0000001) is not valid." );
  check( "a port over 65535",
         refused( { "-p", "65536", "h" } ) == "!gosh: Server-side (low) port (65536) must be within valid range [0..65535]." );
  check( "a range's high port of 0",
         refused( { "-p", "1:0", "h" } ) == "!gosh: Server-side high port (0) must be within valid range [1..65535]." );
  check( "a range from port 0",
         refused( { "-p", "0:5", "h" } ) == "!gosh: Server-side port ranges may not be used with starting port 0 (0:5)." );
  check( "a range backwards",
         refused( { "-p", "9:5", "h" } ) == "!gosh: Server-side port range (9:5): low port greater than high port." );

  /* ---- text */
  check( "split keeps empty parts", split( "a::b", ':' ) == Words( { "a", "", "b" } ) );
  check( "strip_end takes line ends and spaces from the end only", strip_end( " a b \r\n" ) == " a b" );
  check( "all_digits: not empty, digits only", all_digits( "012" ) && !all_digits( "" ) && !all_digits( "1a" ) );
  check( "one_of: whole words only", one_of( "never", "auto never" ) && !one_of( "neve", "auto never" ) );

  if ( failures ) {
    printf( "FAIL REQ1/CMD-1, REQ1/CP-7, REQ1/PATH-7, REQ15/PATH-4, REQ16/PATH-8: %d checks of the command's options failed\n",
            failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/CMD-1, REQ1/CP-7, REQ1/PATH-7, REQ15/PATH-4, REQ16/PATH-8: the command's options\n" );
  return EXIT_SUCCESS;
}
