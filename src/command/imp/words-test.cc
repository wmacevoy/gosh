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

#include "src/command/imp/options.h"
#include "src/command/imp/proxy.h"
#include "src/command/imp/words.h"
#include "vendor/facts/facts.h"

using namespace Gosh::Command;
using namespace Gosh::Command::Imp;



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
/* REQ1/CMD-1, REQ1/CP-1, REQ1/CP-5, REQ1/CP-7, REQ1/PATH-1, REQ8/CMD-3, REQ15/PATH-4: the command's words */
FACTS( Words )
{
  /* ---- words as a shell has them */
  FACT_CHECK( shell_words( " a 'b c'\t\"d \\\" e\" f\\ g \n" ) == Words( { "a", "b c", "d \" e", "f g" } ), "shell_words: quotes and a backslash taken away, blanks between" );
  FACT_CHECK( shell_words( "a '' b" ) == Words( { "a", "", "b" } ), "shell_words: an empty quoted word is a word" );
  FACT_CHECK( shell_quote( { "it's", "a b" } ) == "'it'\\''s' 'a b'", "shell_quote: each word in single quotes, a quote in one closed and opened around it" );
  FACT_CHECK( shell_words( shell_quote( { "it's", "", "$x" } ) )
                                                               == Words( { "it's", "", "$x" } ), "and back: shell_words of shell_quote is the words" );
  std::string host, path;
  FACT_CHECK( ( remote_place( "me@srv:docs/a", host, path ) && host == "me@srv" && path == "docs/a" ), "a remote place: host:path" );
  FACT_CHECK( !remote_place( "./a:b", host, path ), "not one: a colon after a slash" );
  FACT_CHECK( !remote_place( "C:\\x", host, path ), "not one: a Windows drive" );
  FACT_CHECK( !remote_place( "file", host, path ), "not one: no colon" );
  /* (the review of item 10, 2026-11-16) gosh has no Windows drives to mind: a one-letter host is a host */
  FACT_CHECK( ( remote_place( "x:a", host, path ) && host == "x" && path == "a" ), "a one-letter host is a host" );
  FACT_CHECK( ( remote_place( "me@[2001:db8::1]:docs/a", host, path ) && host == "me@2001:db8::1" && path == "docs/a" ), "an IPv6 address in brackets, with a user: ssh gets it without its brackets (REQ17/PATH-8)" );
  FACT_CHECK( ( remote_place( "[::1]:/tmp/x", host, path ) && host == "::1" && path == "/tmp/x" ), "and without one" );
  FACT_CHECK( ( !remote_place( "[]:a", host, path ) && !remote_place( "[::1]a", host, path ) ), "not one: brackets with nothing in them, or no colon after them" );

  /* ---- a copy (REQ1/CP-1, REQ1/CP-5) */
  Asked s;
  FACT_CHECK( ( asked( { "-r", "--auto", "--delete", "-q", "--interval=0.5", "a", "b", "me@srv:dest" }, true, s ).empty()
           && s.host == "me@srv" && s.headless
           && joined( s.stream_local ) == "@copy|send|-r|--auto|--delete|--interval=500|-q|--|a|b"
           && joined( s.stream_remote ) == "@copy|receive|-r|--auto|--delete|--interval=500|--|dest" ), "to the server: here sends, there receives into the place given" << ": " << ( joined( s.stream_local ) + " / " + joined( s.stream_remote ) ) );
  FACT_CHECK( ( asked( { "a", "srv:" }, true, s ).empty()
                                                         && joined( s.stream_remote ) == "@copy|receive|--|." ), "to the server with no path: into its home" );
  FACT_CHECK( ( asked( { "srv:out/*", "srv:b", "." }, true, s ).empty() && s.host == "srv"
           && joined( s.stream_remote ) == "@copy|send|--glob|--|out/*|b" && joined( s.stream_local ) == "@copy|receive|--|." ), "from the server: there sends, expanding its patterns; here receives" );
  FACT_CHECK( joined( s.command ) == "sh|-c|while :; do sleep 86400; done", "a copy has nothing to run: something that waits" );
  FACT_CHECK( asked( { "a" }, true, s ) == "!Usage: mocp ...", "too few words: the copy's usage" );
  FACT_CHECK( asked( { "xx:a", "yy:b" }, true, s ) == "!gocp: one end of a copy has to be this machine.", "both ends remote" );
  FACT_CHECK( asked( { "a", "b" }, true, s ) == "!gocp: one end of a copy has to be another machine: [user@]host:path", "neither end remote" );
  FACT_CHECK( asked( { "xx:a", "yy:b", "." }, true, s ) == "!gocp: the sources have to be on one machine.", "sources on two machines" );
  FACT_CHECK( asked( { "xx:", "." }, true, s ) == "!gocp: what is to be copied from xx?", "a source with no path" );
  FACT_CHECK( asked( { std::string( "a\x1f" ), "x:b" }, true, s )
                                                  == "!gocp: a file name cannot contain the character with code 31.", "a name with the character 31 in it" );
  FACT_CHECK( asked( { "--interval=0.05", "a", "x:b" }, true, s )
                                                     == "!gocp: --interval must be from 0.1 to 2147483 (seconds).", "an interval under a tenth of a second" );
  FACT_CHECK( ( asked( { "--interval=nan", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)."
           && asked( { "--interval=inf", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)."
           && asked( { "--interval=3e9", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)."
           && asked( { "--interval=2x", "a", "x:b" }, true, s ) == "!gocp: --interval must be from 0.1 to 2147483 (seconds)." ), "an interval that is no number, no end, or too large to send (the review of item 10)" );
  FACT_CHECK( ( asked( { "--auto", "--interval=2147483", "a", "x:b" }, true, s ).empty()
                                                        && joined( s.stream_local ).find( "--interval=2147483000" )
                                                             != std::string::npos ), "the largest interval is sent as it fits" );

  /* ---- no word that ssh would take for an option (the review of item 10, 2026-11-16) */
  FACT_CHECK( asked( { "-oProxyCommand=evil", "x" }, false, s ).substr( 0, 1 ) == "!", "a host beginning with - is refused, not given to ssh" );
  FACT_CHECK( asked( { "--", "-oProxyCommand=evil" }, false, s ).substr( 0, 1 ) == "!", "after --, too" );
  FACT_CHECK( asked( { "--", "f", "-oX=y:path" }, true, s ) == "!gocp: a host cannot begin with \"-\": -oX=y", "a copy's remote end beginning with - (after --, where it is not an option of gocp's)" );
  FACT_CHECK( asked( { "-l", "-oX=y", "srv" }, false, s ).substr( 0, 1 ) == "!", "a user beginning with - (by -l)" );
  FACT_CHECK( asked( { "--", "-oX" }, false, s ) == "!gocp: a host cannot begin with \"-\": -oX", "the message says why" );

  /* ---- ssh's ProxyCommand: ssh reads %h and %p in it, so a % in this program's own path is doubled */
  const std::string proxy_line = proxy_command( "/opt/50%off/gosh-client", "inet" );
  FACT_CHECK( proxy_line == "'/opt/50%%off/gosh-client' --mosh-fake-proxy inet %h %p", "the ProxyCommand: this program's path quoted, its % doubled, then %h %p for ssh (the review of item 10)" << ": " << ( proxy_line ) );

  /* ---- -l without --stdio (the review of item 10) */
  FACT_CHECK( ( asked( { "-l", "bob", "srv" }, false, s ).empty()
                                                             && s.host == "bob@srv" ), "-l USER without --stdio: the user is the login's" );
  FACT_CHECK( ( asked( { "-l", "bob", "you@srv" }, false, s ).empty()
                                                      && s.host == "you@srv" ), "and does not replace a user already given" );

  /* ---- --stdio (REQ1/CP-7) */
  FACT_CHECK( ( asked( { "--stdio", "-l", "me", "srv", "rsync", "--server", "." }, false, s ).empty() && s.host == "me@srv"
           && joined( s.stream_remote ) == "@sh|rsync --server ." && joined( s.stream_local ) == "@stdio" && s.headless ), "--stdio: the command's words joined for the shell there; -l becomes the user" << ": " << ( s.host + " / " + joined( s.stream_remote ) ) );
  FACT_CHECK( ( asked( { "--stdio", "srv", "--", "cat" }, false, s ).empty() && joined( s.stream_remote ) == "@sh|cat" ), "--stdio: a -- before the command is not the command's" );
  FACT_CHECK( ( asked( { "--stdio", "--", "srv", "echo", "hi" }, false, s ).empty() && s.host == "srv"
           && joined( s.stream_remote ) == "@sh|echo hi" ), "--stdio -- host command: the -- before the host is not the host (the review of item 10)" << ": " << ( s.host + " / " + joined( s.stream_remote ) ) );
  FACT_CHECK( ( asked( { "--std", "srv", "ls", "-l" }, false, s ).empty() && joined( s.stream_remote ) == "@sh|ls -l" ), "--std, a shortening of --stdio, reads in order as --stdio does" << ": " << ( joined( s.stream_remote ) ) );
  FACT_CHECK( ( asked( { "--stdio", "-l", "me", "you@srv", "cat" }, false, s ).empty() && s.host == "you@srv" ), "--stdio: -l does not replace a user already given" );
  FACT_CHECK( asked( { "--stdio", "srv" }, false, s ) == "!+gocp: --stdio needs a command to run on the server.", "--stdio with no command: refused, with the usage" );

  /* ---- -W HOST:PORT (REQ19/FWD-6) */
  FACT( asked( { "-W", "localhost:22", "srv" }, false, s ) == "", "-W HOST:PORT host: taken" );
  FACT_CHECK( s.host == "srv" );
  FACT_CHECK( joined( s.stream_remote ) == "@connect|localhost|22", "the server joins the stream to HOST:PORT" );
  FACT_CHECK( joined( s.stream_local ) == "@stdio", "and this end to standard input and output" );
  FACT_CHECK( s.headless, "with no terminal" );
  FACT( asked( { "-W", "[::1]:2222", "-l", "me", "srv" }, false, s ) == "", "an IPv6 address in brackets" );
  FACT_CHECK( joined( s.stream_remote ) == "@connect|::1|2222", "is given without them" );
  FACT_CHECK( s.host == "me@srv", "and -l is the login's, as with --stdio" );
  FACT_CHECK( asked( { "-W", "localhost", "srv" }, false, s ) == "!gocp: -W needs HOST:PORT (an IPv6 address in brackets), not \"localhost\".", "no port: refused" );
  FACT_CHECK( asked( { "-W", "h:0", "srv" }, false, s ).substr( 0, 18 ) == "!gocp: -W needs HO", "port 0: refused" );
  FACT_CHECK( asked( { "-W", "h:65536", "srv" }, false, s ).substr( 0, 18 ) == "!gocp: -W needs HO", "a port past 65535: refused" );
  FACT_CHECK( asked( { "-W", "h:ssh", "srv" }, false, s ).substr( 0, 18 ) == "!gocp: -W needs HO", "a service name: refused (a number only)" );
  FACT_CHECK( asked( { "-W", "::1:22", "srv" }, false, s ).substr( 0, 18 ) == "!gocp: -W needs HO", "an IPv6 address without brackets: refused" );
  FACT_CHECK( asked( { "-W", ":22", "srv" }, false, s ).substr( 0, 18 ) == "!gocp: -W needs HO", "no host: refused" );
  FACT_CHECK( asked( { "-W", "h:22", "srv", "ls" }, false, s ) == "!gocp: -W takes the host alone, with no command.", "a command after the host: refused" );
  FACT_CHECK( asked( { "-W", "h:22", "--stdio", "srv", "ls" }, false, s ) == "!gocp: -W and --stdio are two ways of using standard input and output: one at a time.", "with --stdio: refused" );
  FACT_CHECK( asked( { "-W", "h:22", "a", "srv:b" }, true, s ) == "!gocp: -W is not for copying files.", "with a copy: refused" );
  FACT_CHECK( asked( { "-W", "h:22" }, false, s ) == "!+", "with no host: the usage" );

  /* ---- a session */
  FACT_CHECK( ( asked( { "srv", "--", "top", "-d", "1" }, false, s ).empty()
                                                   && s.host == "srv" && joined( s.command ) == "top|-d|1" && !s.headless
                                                   && s.stream_local.empty() ), "a terminal: the host and its command (after --, as its words would be gosh's)" );
  FACT_CHECK( ( asked( { "-N", "srv" }, false, s ).empty() && s.headless
                                                       && joined( s.command ) == "sh|-c|while :; do sleep 86400; done" ), "-N with no command: something that waits" );
  FACT_CHECK( ( asked( { "srv" }, false, s ).empty() && s.command.empty() ), "a terminal with no command: none (the login shell)" );
  FACT_CHECK( asked( { "-N" }, false, s ) == "!+", "no host: the usage" );

  FACT_CHECK( ( joined( bind_arguments( "", false, "h" ) ) == "-s"
                                                    && joined( bind_arguments( "SSH", true, "u@h" ) ) == "-i|u@h" ), "bind: by ssh, -s; --local, -i the host" );
  FACT_CHECK( ( bind_arguments( "Any", false, "h" ).empty()
                                                   && joined( bind_arguments( "10.0.0.1", false, "h" ) ) == "-i|10.0.0.1" ), "bind: any, nothing; an address, -i it" );

  /* ---- the server's words (REQ8/CMD-3) */
  Settings fs = settings( { "-p", "60001", "-L", "8:a:80", "-RU", "53:ns:53", "--fragment-parity=always", "-N", "srv" } );
  Choices c = choose( fs, "gosh", NULL );
  Asked hs = what_was_asked( fs, false, false, "gosh", COPY_USAGE );
  ServerWords w = server_words( fs, c, hs, "256", { "-s" }, { "LANG=C.UTF-8" } );
  FACT_CHECK( joined( w.gosh ) == "new|-c|256|-s|-p|60001|-l|LANG=C.UTF-8|-RU|53:ns:53|-L|8:a:80|-F|always|--|sh|-c|while :; do sleep 86400; done", "gosh-server: new, colours, bind, port, locales, forwards (UDP first), parity, the command" << ": " << ( joined( w.gosh ) ) );
  FACT_CHECK( joined( w.stock ) == "new|-c|256|-s|-p|60001|-l|LANG=C.UTF-8|--|sh|-c|while :; do sleep 86400; done", "a stock mosh-server: the same less the forwards and the parity" << ": " << ( joined( w.stock ) ) );
  FACT_CHECK( joined( w.forwards ) == "-RU|53:ns:53|-L|8:a:80|-F|always", "the forwards, for the client too, in pairs" );
  Settings cs = settings( { "a", "srv:b" } );
  Asked copy = what_was_asked( cs, true, false, "gocp", COPY_USAGE );
  w = server_words( cs, choose( cs, "gocp", NULL ), copy, "0", { "-s" }, {} );
  FACT_CHECK( ( joined( w.gosh ) == "new|-c|0|-s|-X|@copy\x1freceive\x1f--\x1f" "b|--|sh|-c|while :; do sleep 86400; done"
           && joined( w.stock ) == "new|-c|0|-s|--|sh|-c|while :; do sleep 86400; done" ), "a stream's far end goes to gosh-server as -X, its words joined by character 31, and to no stock one" << ": " << ( joined( w.gosh ) ) );
  FACT_CHECK( server_line( false, "/opt/gs", w ) == "/opt/gs " + shell_quote( w.gosh ), "a server the user named is given all of gosh-server's words" );
  FACT_CHECK( ( server_line( true, "/opt/gs", w ).compare( 0, 7, "sh -c '" ) == 0
                                                   && server_line( true, "/opt/gs", w ).find( "/opt/gs" ) == std::string::npos ), "otherwise the host chooses (stock.h)" );

  /* ---- ssh's words (REQ1/CMD-1) */
  Choices proxy = choose( settings( { "h" } ), "gosh", NULL );
  FACT_CHECK( joined( login_words( { "ssh", "-p", "2" }, true, proxy, "PX %h %p", "me@h", "LINE" ) )
           == "ssh|-p|2|-n|-tt|-S|none|-o|ProxyCommand=PX %h %p|me@h|--|LINE", "by proxy: -n -tt, no ControlMaster, this program as the ProxyCommand" );
  Choices remote = choose( settings( { "--experimental-remote-ip=remote", "-6", "h" } ), "gosh", NULL );
  const Words rw = login_words( { "ssh" }, false, remote, "", "h", "LINE" );
  FACT_CHECK( ( rw.size() == 6 && joined( Words( rw.begin(), rw.begin() + 5 ) ) == "ssh|-n|-6|h|--" && rw[5].compare( 0, 6, "sh -c " ) == 0
           && rw[5].find( "MOSH SSH_CONNECTION" ) != std::string::npos
           && rw[5].compare( rw[5].size() - 7, 7, " ; LINE" ) == 0 ), "by remote: no -tt with --no-ssh-pty, -6, and the server asked its address first" << ": " << ( joined( rw ) ) );
  Choices local = choose( settings( { "--experimental-remote-ip=local", "-4", "h" } ), "gosh", NULL );
  FACT_CHECK( joined( login_words( { "ssh" }, true, local, "", "h", "LINE" ) ) == "ssh|-n|-tt|h|--|LINE", "by local: ssh is asked nothing more, not even -4" );

  /* ---- the relay and the ticket (REQ1/PATH-1, REQ15/PATH-4) */
  const Words relay = relay_command( { "ssh", "-p", "2" }, false, true, "ignored", "me@h", "10.0.0.1", "60001" );
  FACT_CHECK( ( relay.size() == 9 && joined( Words( relay.begin(), relay.begin() + 8 ) ) == "ssh|-p|2|-T|-o|BatchMode=yes|me@h|--" ), "the relay: ssh with nothing to be asked, to gosh-server's relay with the address and port" << ": " << ( joined( relay ) ) );
  FACT_CHECK( ( relay.back().find( "exec gosh-server relay '\\''10.0.0.1'\\'' '\\''60001'\\''" ) != std::string::npos
           && relay.back().find( "mosh-server" ) == std::string::npos ), "  (its last word runs gosh-server, never mosh-server)" << ": " << ( relay.back() ) );
  FACT_CHECK( relay_command( { "ssh" }, false, false, "/opt/gs", "h", "1.2.3.4", "5" ).back() == "/opt/gs relay '1.2.3.4' '5'", "a relay with a server the user named runs that one" );
  FACT_CHECK( joined( relay_command( { "ssh" }, true, false, "/opt/gs", "h", "1.2.3.4", "5" ) )
                                                 == "sh|-c|/opt/gs relay '1.2.3.4' '5'", "--local: the relay by a shell here" );
  const Words ticket = ticket_command( "me@meet" );
  FACT_CHECK( ( joined( Words( ticket.begin(), ticket.end() - 1 ) ) == "ssh|-T|-o|BatchMode=yes|me@meet|--"
           && ticket.back().find( "exec gosh-server meetup-ticket" ) != std::string::npos ), "the ticket: ssh to the meetup with nothing to be asked, for gosh-server's meetup-ticket" );
  /* (issue #5) no case for --server or --ssh: neither is passed in, so neither can reach the meetup; the
     ssh words of the ticket above are plain ssh's (seen failing when --ssh's "ssh -p 2222" was passed in) */

  /* ---- the client */
  FACT_CHECK( joined( client_words( { "-N", "srv" }, { "-L", "1:a:1" }, hs, "10.0.0.1", "60001" ) )
           == "mosh-client|-# -N srv ||-L|1:a:1|-N|10.0.0.1|60001", "the client: what was asked for whoever lists processes, the forwards, -N, where the server is" );
  FACT_CHECK( joined( client_words( { "a", "srv:b" }, {}, copy, "10.0.0.1", "60001" ) )
           == "mosh-client|-# a srv:b ||-X|@copy\x1fsend\x1f--\x1f" "a|10.0.0.1|60001", "a stream's near end instead of -N" );
  Asked terminal;
  terminal.host = "srv";
  FACT_CHECK( joined( client_words( { "srv" }, {}, terminal, "h", "1" ) ) == "mosh-client|-# srv ||h|1", "a terminal: neither" );

  const Variables v = client_variables( "KEY", proxy, false, true );
  FACT_CHECK( ( v.size() == 4 && v[0].first == "MOSH_KEY" && v[0].second == "KEY" && v[1].first == "MOSH_PREDICTION_DISPLAY"
           && v[1].second == "adaptive" && v[2].first == "MOSH_NO_TERM_INIT" && v[2].second == "1"
           && v[3].first == "MOSH_PREDICTION_OVERWRITE" && v[3].second == "yes" ), "the client's variables: the key, the prediction; no terminal set-up; overwrite" );
  FACT_CHECK( client_variables( "KEY", proxy, true, false ).size() == 2, "and by default only the first two" );

  Choices meet = choose( settings( { "--meetup=me@meet:3478", "h" } ), "gosh", NULL );
  Variables rv = relay_variables( meet, false, { "ssh", "h" }, { "ssh", "meet" } );
  FACT_CHECK( ( rv.size() == 3 && rv[0].first == "GOSH_RELAY_COMMAND" && rv[0].second == "ssh\x1fh" && rv[1].first == "GOSH_RELAY"
           && rv[1].second == "auto" && rv[2].first == "GOSH_MEETUP_TICKET_COMMAND" && rv[2].second == "ssh\x1fmeet" ), "the relay's variables: its command joined by character 31, its mode, the ticket's command" );
  FACT_CHECK( relay_variables( proxy, false, { "ssh" }, { "ssh" } ).size() == 2, "no meetup, no ticket's command" );
  rv = relay_variables( meet, true, { "ssh" }, { "ssh" } );
  FACT_CHECK( ( rv.size() == 1 && rv[0].first == "GOSH_RELAY" && rv[0].second == "never" ), "a stock server: no relay (REQ8/CMD-3)" );
  rv = relay_variables( choose( settings( { "--relay=never", "h" } ), "gosh", NULL ), false, { "ssh" }, { "ssh" } );
  FACT_CHECK( ( rv.size() == 1 && rv[0].second == "never" ), "--relay=never: none" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Words );
}

FACTS_MAIN
