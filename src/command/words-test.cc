/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * The command's words for each program (words.h): which end of a copy is which and what each is
 * told (REQ1/CP-1, REQ1/CP-5); --stdio's command (REQ1/CP-7); the server's words, and a stock
 * server's (REQ8/CMD-3); ssh's (REQ1/CMD-1); the relay's and the meetup ticket's commands
 * (REQ1/PATH-1, REQ15/PATH-4); the client's words and variables. Plain strings.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/command/options.h"
#include "src/command/proxy.h"
#include "src/command/words.h"

using namespace Gosh::Command;

static int failures = 0;

static void check( const char* what, bool ok, const std::string& got = "" )
{
  printf( "%-88s %s%s\n", what, ok ? "ok" : "FAIL ", ok ? "" : got.c_str() );
  if ( !ok ) {
    failures++;
  }
}

static const char COPY_USAGE[] = "Usage: mocp ...";

static Settings settings( const Words& words, bool in_order = false )
{
  Settings s;
  parse_options( words, in_order, s );
  return s;
}

/* The session asked for, or the failure's message, prefixed "!" (and "+" with the usage). */
static std::string asked( const Words& words, bool copy, Asked& session )
{
  try {
    const Settings s = settings( words, wants_stdio( words ) );
    session = what_was_asked( s, copy, s.flag( "stdio" ), "gocp", COPY_USAGE );
  } catch ( const Failure& f ) {
    return std::string( "!" ) + ( f.with_usage ? "+" : "" ) + f.message;
  }
  return "";
}

static std::string joined( const Words& w )
{
  return join( w, "|" );
}

int main()
{
  /* ---- words as a shell has them */
  check( "shell_words: quotes and a backslash taken away, blanks between",
         shell_words( " a 'b c'\t\"d \\\" e\" f\\ g \n" ) == Words( { "a", "b c", "d \" e", "f g" } ) );
  check( "shell_words: an empty quoted word is a word", shell_words( "a '' b" ) == Words( { "a", "", "b" } ) );
  check( "shell_quote: each word in single quotes, a quote in one closed and opened around it",
         shell_quote( { "it's", "a b" } ) == "'it'\\''s' 'a b'" );
  check( "and back: shell_words of shell_quote is the words", shell_words( shell_quote( { "it's", "", "$x" } ) )
                                                               == Words( { "it's", "", "$x" } ) );
  std::string host, path;
  check( "a remote place: host:path", remote_place( "me@srv:docs/a", host, path ) && host == "me@srv" && path == "docs/a" );
  check( "not one: a colon after a slash", !remote_place( "./a:b", host, path ) );
  check( "not one: a Windows drive", !remote_place( "C:\\x", host, path ) );
  check( "not one: no colon", !remote_place( "file", host, path ) );
  /* (the review of item 10, 2026-11-16) gosh has no Windows drives to mind: a one-letter host is a host */
  check( "a one-letter host is a host", remote_place( "x:a", host, path ) && host == "x" && path == "a" );
  check( "an IPv6 address in brackets, with a user: ssh gets it without its brackets (REQ17/PATH-8)",
         remote_place( "me@[2001:db8::1]:docs/a", host, path ) && host == "me@2001:db8::1" && path == "docs/a" );
  check( "and without one", remote_place( "[::1]:/tmp/x", host, path ) && host == "::1" && path == "/tmp/x" );
  check( "not one: brackets with nothing in them, or no colon after them",
         !remote_place( "[]:a", host, path ) && !remote_place( "[::1]a", host, path ) );

  /* ---- a copy (REQ1/CP-1, REQ1/CP-5) */
  Asked s;
  check( "to the server: here sends, there receives into the place given",
         asked( { "-r", "--auto", "--delete", "-q", "--interval=0.5", "a", "b", "me@srv:dest" }, true, s ).empty()
           && s.host == "me@srv" && s.headless
           && joined( s.stream_local ) == "@copy|send|-r|--auto|--delete|--interval=500|-q|--|a|b"
           && joined( s.stream_remote ) == "@copy|receive|-r|--auto|--delete|--interval=500|--|dest",
         joined( s.stream_local ) + " / " + joined( s.stream_remote ) );
  check( "to the server with no path: into its home", asked( { "a", "srv:" }, true, s ).empty()
                                                         && joined( s.stream_remote ) == "@copy|receive|--|." );
  check( "from the server: there sends, expanding its patterns; here receives",
         asked( { "srv:out/*", "srv:b", "." }, true, s ).empty() && s.host == "srv"
           && joined( s.stream_remote ) == "@copy|send|--glob|--|out/*|b" && joined( s.stream_local ) == "@copy|receive|--|." );
  check( "a copy has nothing to run: something that waits",
         joined( s.command ) == "sh|-c|while :; do sleep 86400; done" );
  check( "too few words: the copy's usage", asked( { "a" }, true, s ) == "!Usage: mocp ..." );
  check( "both ends remote", asked( { "xx:a", "yy:b" }, true, s ) == "!gocp: one end of a copy has to be this machine." );
  check( "neither end remote", asked( { "a", "b" }, true, s ) == "!gocp: one end of a copy has to be another machine: [user@]host:path" );
  check( "sources on two machines", asked( { "xx:a", "yy:b", "." }, true, s ) == "!gocp: the sources have to be on one machine." );
  check( "a source with no path", asked( { "xx:", "." }, true, s ) == "!gocp: what is to be copied from xx?" );
  check( "a name with the character 31 in it", asked( { std::string( "a\x1f" ), "x:b" }, true, s )
                                                  == "!gocp: a file name cannot contain the character with code 31." );
  check( "an interval under a tenth of a second", asked( { "--interval=0.05", "a", "x:b" }, true, s )
                                                     == "!gocp: --interval must be from 0.1 to 2147483 (seconds)." );
  check( "an interval that is no number, no end, or too large to send (the review of item 10)",
         asked( { "--interval=nan", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)."
           && asked( { "--interval=inf", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)."
           && asked( { "--interval=3e9", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)."
           && asked( { "--interval=2x", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)." );
  check( "the largest interval is sent as it fits", asked( { "--auto", "--interval=2147483", "a", "x:b" }, true, s ).empty()
                                                        && joined( s.stream_local ).find( "--interval=2147483000" )
                                                             != std::string::npos );

  /* ---- no word that ssh would take for an option (the review of item 10, 2026-11-16) */
  check( "a host beginning with - is refused, not given to ssh",
         asked( { "-oProxyCommand=evil", "x" }, false, s ).substr( 0, 1 ) == "!" );
  check( "after --, too", asked( { "--", "-oProxyCommand=evil" }, false, s ).substr( 0, 1 ) == "!" );
  check( "a copy's remote end beginning with - (after --, where it is not an option of gocp's)",
         asked( { "--", "f", "-oX=y:path" }, true, s ) == "!gocp: a host cannot begin with \"-\": -oX=y" );
  check( "a user beginning with - (by -l)", asked( { "-l", "-oX=y", "srv" }, false, s ).substr( 0, 1 ) == "!" );
  check( "the message says why", asked( { "--", "-oX" }, false, s ) == "!gocp: a host cannot begin with \"-\": -oX" );

  /* ---- ssh's ProxyCommand: ssh reads %h and %p in it, so a % in this program's own path is doubled */
  const std::string proxy_line = proxy_command( "/opt/50%off/gosh-client", "inet" );
  check( "the ProxyCommand: this program's path quoted, its % doubled, then %h %p for ssh (the review of item 10)",
         proxy_line == "'/opt/50%%off/gosh-client' --mosh-fake-proxy inet %h %p", proxy_line );

  /* ---- -l without --stdio (the review of item 10) */
  check( "-l USER without --stdio: the user is the login's", asked( { "-l", "bob", "srv" }, false, s ).empty()
                                                             && s.host == "bob@srv" );
  check( "and does not replace a user already given", asked( { "-l", "bob", "you@srv" }, false, s ).empty()
                                                      && s.host == "you@srv" );

  /* ---- --stdio (REQ1/CP-7) */
  check( "--stdio: the command's words joined for the shell there; -l becomes the user",
         asked( { "--stdio", "-l", "me", "srv", "rsync", "--server", "." }, false, s ).empty() && s.host == "me@srv"
           && joined( s.stream_remote ) == "@sh|rsync --server ." && joined( s.stream_local ) == "@stdio" && s.headless,
         s.host + " / " + joined( s.stream_remote ) );
  check( "--stdio: a -- before the command is not the command's",
         asked( { "--stdio", "srv", "--", "cat" }, false, s ).empty() && joined( s.stream_remote ) == "@sh|cat" );
  check( "--stdio -- host command: the -- before the host is not the host (the review of item 10)",
         asked( { "--stdio", "--", "srv", "echo", "hi" }, false, s ).empty() && s.host == "srv"
           && joined( s.stream_remote ) == "@sh|echo hi",
         s.host + " / " + joined( s.stream_remote ) );
  check( "--std, a shortening of --stdio, reads in order as --stdio does",
         asked( { "--std", "srv", "ls", "-l" }, false, s ).empty() && joined( s.stream_remote ) == "@sh|ls -l",
         joined( s.stream_remote ) );
  check( "--stdio: -l does not replace a user already given",
         asked( { "--stdio", "-l", "me", "you@srv", "cat" }, false, s ).empty() && s.host == "you@srv" );
  check( "--stdio with no command: refused, with the usage",
         asked( { "--stdio", "srv" }, false, s ) == "!+gocp: --stdio needs a command to run on the server." );

  /* ---- a session */
  check( "a terminal: the host and its command (after --, as its words would be gosh's)",
         asked( { "srv", "--", "top", "-d", "1" }, false, s ).empty()
                                                   && s.host == "srv" && joined( s.command ) == "top|-d|1" && !s.headless
                                                   && s.stream_local.empty() );
  check( "-N with no command: something that waits", asked( { "-N", "srv" }, false, s ).empty() && s.headless
                                                       && joined( s.command ) == "sh|-c|while :; do sleep 86400; done" );
  check( "a terminal with no command: none (the login shell)", asked( { "srv" }, false, s ).empty() && s.command.empty() );
  check( "no host: the usage", asked( { "-N" }, false, s ) == "!+" );

  check( "bind: by ssh, -s; --local, -i the host", joined( bind_arguments( "", false, "h" ) ) == "-s"
                                                    && joined( bind_arguments( "SSH", true, "u@h" ) ) == "-i|u@h" );
  check( "bind: any, nothing; an address, -i it", bind_arguments( "Any", false, "h" ).empty()
                                                   && joined( bind_arguments( "10.0.0.1", false, "h" ) ) == "-i|10.0.0.1" );

  /* ---- the server's words (REQ8/CMD-3) */
  Settings fs = settings( { "-p", "60001", "-L", "8:a:80", "-RU", "53:ns:53", "--fragment-parity=always", "-N", "srv" } );
  Choices c = choose( fs, "gosh", NULL );
  Asked hs = what_was_asked( fs, false, false, "gosh", COPY_USAGE );
  ServerWords w = server_words( fs, c, hs, "256", { "-s" }, { "LANG=C.UTF-8" } );
  check( "gosh-server: new, colours, bind, port, locales, forwards (UDP first), parity, the command",
         joined( w.gosh ) == "new|-c|256|-s|-p|60001|-l|LANG=C.UTF-8|-RU|53:ns:53|-L|8:a:80|-F|always|--|sh|-c|while :; do sleep 86400; done",
         joined( w.gosh ) );
  check( "a stock mosh-server: the same less the forwards and the parity",
         joined( w.stock ) == "new|-c|256|-s|-p|60001|-l|LANG=C.UTF-8|--|sh|-c|while :; do sleep 86400; done", joined( w.stock ) );
  check( "the forwards, for the client too, in pairs", joined( w.forwards ) == "-RU|53:ns:53|-L|8:a:80|-F|always" );
  Settings cs = settings( { "a", "srv:b" } );
  Asked copy = what_was_asked( cs, true, false, "gocp", COPY_USAGE );
  w = server_words( cs, choose( cs, "gocp", NULL ), copy, "0", { "-s" }, {} );
  check( "a stream's far end goes to gosh-server as -X, its words joined by character 31, and to no stock one",
         joined( w.gosh ) == "new|-c|0|-s|-X|@copy\x1freceive\x1f--\x1f" "b|--|sh|-c|while :; do sleep 86400; done"
           && joined( w.stock ) == "new|-c|0|-s|--|sh|-c|while :; do sleep 86400; done",
         joined( w.gosh ) );
  check( "a server the user named is given all of gosh-server's words",
         server_line( false, "/opt/gs", w ) == "/opt/gs " + shell_quote( w.gosh ) );
  check( "otherwise the host chooses (stock.h)", server_line( true, "/opt/gs", w ).compare( 0, 7, "sh -c '" ) == 0
                                                   && server_line( true, "/opt/gs", w ).find( "/opt/gs" ) == std::string::npos );

  /* ---- ssh's words (REQ1/CMD-1) */
  Choices proxy = choose( settings( { "h" } ), "gosh", NULL );
  check( "by proxy: -n -tt, no ControlMaster, this program as the ProxyCommand",
         joined( login_words( { "ssh", "-p", "2" }, true, proxy, "PX %h %p", "me@h", "LINE" ) )
           == "ssh|-p|2|-n|-tt|-S|none|-o|ProxyCommand=PX %h %p|me@h|--|LINE" );
  Choices remote = choose( settings( { "--experimental-remote-ip=remote", "-6", "h" } ), "gosh", NULL );
  const Words rw = login_words( { "ssh" }, false, remote, "", "h", "LINE" );
  check( "by remote: no -tt with --no-ssh-pty, -6, and the server asked its address first",
         rw.size() == 6 && joined( Words( rw.begin(), rw.begin() + 5 ) ) == "ssh|-n|-6|h|--" && rw[5].compare( 0, 6, "sh -c " ) == 0
           && rw[5].find( "MOSH SSH_CONNECTION" ) != std::string::npos
           && rw[5].compare( rw[5].size() - 7, 7, " ; LINE" ) == 0,
         joined( rw ) );
  Choices local = choose( settings( { "--experimental-remote-ip=local", "-4", "h" } ), "gosh", NULL );
  check( "by local: ssh is asked nothing more, not even -4",
         joined( login_words( { "ssh" }, true, local, "", "h", "LINE" ) ) == "ssh|-n|-tt|h|--|LINE" );

  /* ---- the relay and the ticket (REQ1/PATH-1, REQ15/PATH-4) */
  const Words relay = relay_command( { "ssh", "-p", "2" }, false, true, "ignored", "me@h", "10.0.0.1", "60001" );
  check( "the relay: ssh with nothing to be asked, to gosh-server's relay with the address and port",
         relay.size() == 9 && joined( Words( relay.begin(), relay.begin() + 8 ) ) == "ssh|-p|2|-T|-o|BatchMode=yes|me@h|--",
         joined( relay ) );
  check( "  (its last word runs gosh-server, never mosh-server)",
         relay.back().find( "exec gosh-server relay '\\''10.0.0.1'\\'' '\\''60001'\\''" ) != std::string::npos
           && relay.back().find( "mosh-server" ) == std::string::npos,
         relay.back() );
  check( "a relay with a server the user named runs that one",
         relay_command( { "ssh" }, false, false, "/opt/gs", "h", "1.2.3.4", "5" ).back() == "/opt/gs relay '1.2.3.4' '5'" );
  check( "--local: the relay by a shell here", joined( relay_command( { "ssh" }, true, false, "/opt/gs", "h", "1.2.3.4", "5" ) )
                                                 == "sh|-c|/opt/gs relay '1.2.3.4' '5'" );
  const Words ticket = ticket_command( { "ssh" }, true, "ignored", "me@meet" );
  check( "the ticket: ssh to the meetup with nothing to be asked, for gosh-server's meetup-ticket",
         joined( Words( ticket.begin(), ticket.end() - 1 ) ) == "ssh|-T|-o|BatchMode=yes|me@meet|--"
           && ticket.back().find( "exec gosh-server meetup-ticket" ) != std::string::npos );
  check( "a ticket with a server the user named", ticket_command( { "ssh" }, false, "/opt/gs", "m" ).back() == "/opt/gs meetup-ticket" );

  /* ---- the client */
  check( "the client: what was asked for whoever lists processes, the forwards, -N, where the server is",
         joined( client_words( { "-N", "srv" }, { "-L", "1:a:1" }, hs, "10.0.0.1", "60001" ) )
           == "mosh-client|-# -N srv ||-L|1:a:1|-N|10.0.0.1|60001" );
  check( "a stream's near end instead of -N",
         joined( client_words( { "a", "srv:b" }, {}, copy, "10.0.0.1", "60001" ) )
           == "mosh-client|-# a srv:b ||-X|@copy\x1fsend\x1f--\x1f" "a|10.0.0.1|60001" );
  Asked terminal;
  terminal.host = "srv";
  check( "a terminal: neither", joined( client_words( { "srv" }, {}, terminal, "h", "1" ) ) == "mosh-client|-# srv ||h|1" );

  const Variables v = client_variables( "KEY", proxy, false, true );
  check( "the client's variables: the key, the prediction; no terminal set-up; overwrite",
         v.size() == 4 && v[0].first == "MOSH_KEY" && v[0].second == "KEY" && v[1].first == "MOSH_PREDICTION_DISPLAY"
           && v[1].second == "adaptive" && v[2].first == "MOSH_NO_TERM_INIT" && v[2].second == "1"
           && v[3].first == "MOSH_PREDICTION_OVERWRITE" && v[3].second == "yes" );
  check( "and by default only the first two", client_variables( "KEY", proxy, true, false ).size() == 2 );

  Choices meet = choose( settings( { "--meetup=me@meet:3478", "h" } ), "gosh", NULL );
  Variables rv = relay_variables( meet, false, { "ssh", "h" }, { "ssh", "meet" } );
  check( "the relay's variables: its command joined by character 31, its mode, the ticket's command",
         rv.size() == 3 && rv[0].first == "GOSH_RELAY_COMMAND" && rv[0].second == "ssh\x1fh" && rv[1].first == "GOSH_RELAY"
           && rv[1].second == "auto" && rv[2].first == "GOSH_MEETUP_TICKET_COMMAND" && rv[2].second == "ssh\x1fmeet" );
  check( "no meetup, no ticket's command", relay_variables( proxy, false, { "ssh" }, { "ssh" } ).size() == 2 );
  rv = relay_variables( meet, true, { "ssh" }, { "ssh" } );
  check( "a stock server: no relay (REQ8/CMD-3)", rv.size() == 1 && rv[0].first == "GOSH_RELAY" && rv[0].second == "never" );
  rv = relay_variables( choose( settings( { "--relay=never", "h" } ), "gosh", NULL ), false, { "ssh" }, { "ssh" } );
  check( "--relay=never: none", rv.size() == 1 && rv[0].second == "never" );

  if ( failures ) {
    printf( "FAIL REQ1/CMD-1, REQ1/CP-1, REQ1/CP-5, REQ1/CP-7, REQ1/PATH-1, REQ8/CMD-3, REQ15/PATH-4: %d checks of the "
            "command's words failed\n",
            failures );
    return EXIT_FAILURE;
  }
  printf( "PASS REQ1/CMD-1, REQ1/CP-1, REQ1/CP-5, REQ1/CP-7, REQ1/PATH-1, REQ8/CMD-3, REQ15/PATH-4: the command's words\n" );
  return EXIT_SUCCESS;
}
