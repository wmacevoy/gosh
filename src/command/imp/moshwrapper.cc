/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

/*
 * the command: the pieces strung together (design v2, 2.2). What it decides is options.h's,
 * words.h's, startup.h's and stock.h's; login.h and proxy.h are the edges it starts things with.
 * What is left here is the order, the usage, and the two places it touches the world itself: the
 * environment it reads (prediction, locales) and the variables it sets for the client.
 */

#include "src/include/config.h"

#include <cstdio>
#include <cstdlib>

#include <string>
#include <vector>

#include "src/command/imp/login.h"
#include "src/command/moshwrapper.h"
#include "src/command/imp/options.h"
#include "src/command/imp/proxy.h"
#include "src/ssh/config.h"
#include "src/command/imp/startup.h"
#include "src/command/imp/stock.h"
#include "src/command/imp/words.h"
#include "src/include/version.h"
#include "src/seam/gosh-version.h"

using namespace Gosh;
using namespace Gosh::Command;
using namespace Gosh::Command::Imp;

namespace {

const char USAGE[] = R"USAGE(Usage: %s [options] [--] [user@]host [command...]
        --client=PATH        the client on this machine (accepted, and not used:
                                this program is the client)
        --server=COMMAND     the server on the remote machine
                                (default: gosh-server, or mosh-server where
                                there is none; looked for in ~/.local/bin too)

        --predict=adaptive      local echo for slower links [default]
-a      --predict=always        use local echo even on fast links
-n      --predict=never         never use local echo
        --predict=experimental  aggressively echo even when incorrect

-o      --predict-overwrite     prediction overwrites instead of inserting

-4      --family=inet        use IPv4 only
-6      --family=inet6       use IPv6 only
        --family=auto        autodetect network type for single-family hosts only
        --family=all         try all network types
        --family=prefer-inet use all network types, but try IPv4 first [default]
        --family=prefer-inet6 use all network types, but try IPv6 first
-p PORT[:PORT2]
        --port=PORT[:PORT2]  server-side UDP port or range
                                (No effect on server-side SSH port)
        --bind-server={ssh|any|IP}  ask the server to reply from an IP address
                                       (default: "ssh")

-LU [BIND_ADDRESS:]PORT:HOST:HOSTPORT
        --udp-forward=[BIND_ADDRESS:]PORT:HOST:HOSTPORT
                             forward UDP datagrams sent to local PORT
                                to HOST:HOSTPORT as seen from the server
                                (may be repeated; needs support on the server)
-RU [BIND_ADDRESS:]PORT:HOST:HOSTPORT
        --udp-remote-forward=[BIND_ADDRESS:]PORT:HOST:HOSTPORT
                             forward UDP datagrams sent to PORT on the
                                server to HOST:HOSTPORT as seen from here

        --fragment-parity={auto|always|never}
                             whether to add parity to forwarded UDP
                                datagrams too large to send whole, so that
                                they survive the loss of one fragment
                                (default: "auto", when the network is
                                losing enough to make it worthwhile)

-L [BIND_ADDRESS:]PORT:HOST:HOSTPORT
        --tcp-forward=[BIND_ADDRESS:]PORT:HOST:HOSTPORT
                             forward TCP connections made to local PORT
                                to HOST:HOSTPORT as seen from the server,
                                as "ssh -L" does
                                (may be repeated; needs support on the server)
-R [BIND_ADDRESS:]PORT:HOST:HOSTPORT
        --tcp-remote-forward=[BIND_ADDRESS:]PORT:HOST:HOSTPORT
                             forward TCP connections made to PORT on the
                                server to HOST:HOSTPORT as seen from here,
                                as "ssh -R" does
                                (may be repeated; needs support on the server)

-N                           no terminal: just keep the session, and the
                                forwards given with it, open until
                                interrupted (as "ssh -N" does)

        --stdio              no terminal: run the command on the server
                                with its input and output here, as
                                "ssh host command" does

-W HOST:PORT                 no terminal: join standard input and output
                                to HOST:PORT as seen from the server, as
                                "ssh -W" does; for ssh's ProxyCommand:
                                "ProxyCommand gosh -W localhost:22 %%r@%%h"
                                (see USING.md for the rest of the block)

        --copy               copy files: see "gocp --help"

        --relay={auto|always|never}
                             carry the session over ssh when UDP cannot
                                reach the server: when needed (default),
                                from the start and nothing else, or never

        --punch={auto|never}  when the session is being carried over ssh,
                                look for a way straight to the server by
                                UDP through the NATs in between, and use
                                it instead (default: auto)
        --meetup=[USER@]HOST[:PORT]  a meetup of your own for that (gosh-server
                                meetup [PORT], port 3478 if none, on a
                                machine ssh logs you in to): it says
                                where each end appears to be,
                                and passes the session on by UDP when the
                                ends can find no way to each other; its
                                ticket is fetched over ssh
        --stun=HOST:PORT[,...]  plain STUN servers to ask where each end
                                appears to be; "none" to ask none
                                (default: none; a meetup answers it)

        --ssh=COMMAND        ssh command to run when setting up session
                                (example: "ssh -p 2222")
                                (default: "ssh"; a meetup's ticket is
                                fetched by plain ssh, as its ssh config says)

        --no-ssh-pty         do not allocate a pseudo tty on ssh connection

        --no-init            do not send terminal initialization string

        --local              run the server on this machine, without ssh

        --experimental-remote-ip=(local|remote|proxy)  select the method for
                             discovering the remote IP address to use
                             (default: "proxy")

        --help               this message
        --version            version and copyright information

gosh: mosh's session, with forwards, copies, the relay over ssh and path-finding
beside it. Not independently reviewed. https://wmacevoy.github.io/gosh/
Not the mosh project's work: report problems at https://github.com/wmacevoy/gosh/issues.
)USAGE";

const char COPY_USAGE[] = R"USAGE(Usage: gocp [options] SOURCE... DESTINATION

Copies files to or from another machine through a gosh session, so that the
copy survives losing the network, changing networks, and sleep, and carries
on where it left off. Exactly one end is remote: [user@]host:path

  gocp report.pdf server:docs/         to the server
  gocp -r server:build/out .           from it, with what is inside
  gocp --auto -r server:build/out .    and then keep ./out up to date

-r                  directories, and everything in them
        --auto      when the copy is done, stay, and copy again whatever
                       changes at the source (until interrupted)
        --delete    also remove from the destination what is no longer
                       at the source (within the directories copied)
        --interval=SECONDS
                    how often --auto looks for changes (default: 2)
-q                  say only what goes wrong

A file is replaced only when the whole of it has arrived. Files that are
already the same (by size and time) are not sent again. Options of gosh
that concern the connection (--ssh, --relay, -p, --server, ...) work here.
)USAGE";

/* The server, unless told otherwise: mosh-server, looked for in ~/.local/bin as well (see scripts/mosh.pl). */
const char DEFAULT_SERVER[] = R"(sh -c 'PATH="$HOME/.local/bin:$PATH"; export PATH; exec mosh-server "$@"' mosh-server)";

const char* const LOCALE_NAMES[] = { "LANG", "LANGUAGE", "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE",
                                     "LC_MONETARY", "LC_MESSAGES", "LC_PAPER", "LC_NAME", "LC_ADDRESS",
                                     "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION", "LC_ALL" };

void set_variables( const Variables& variables )
{
  for ( size_t i = 0; i < variables.size(); i++ ) {
    setenv( variables[i].first.c_str(), variables[i].second.c_str(), 1 );
  }
}

}

bool Gosh::Command::run( const std::vector<std::string>& args,
                       bool copy,
                       const std::string& name,
                       const std::string& self,
                       void ( *print_colors )( void ),
                       std::vector<std::string>& client_args,
                       int& status )
{
  status = 1;
  try {
    Settings s;
    /*
     * With --stdio we stand where ssh would, for a program such as rsync
     * that runs "ssh [-l user] host command -with --options": what follows
     * the host is the command's, not ours.
     */
    parse_options( args, wants_stdio( args ), s );
    copy = copy || s.flag( "copy" );
    const bool stdio = s.flag( "stdio" );

    if ( s.flag( "help" ) ) {
      if ( copy ) {
        fputs( COPY_USAGE, stdout );
      } else {
        printf( USAGE, name.c_str() );
      }
      status = 0;
      return false;
    }
    if ( s.flag( "version" ) ) {
      printf( "gosh %s\n%s [build %s]\n"
              "Copyright 2012 Keith Winstein <mosh-devel@mit.edu>\n"
              "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
              "This is free software: you are free to change and redistribute it.\n"
              "There is NO WARRANTY, to the extent permitted by law.\n",
              GOSH_VERSION, PACKAGE_STRING, BUILD_VERSION );
      status = 0;
      return false;
    }

    Choices choices = choose( s, name, getenv( "MOSH_PREDICTION_DISPLAY" ) );

    /* ---- what the session is for */

    const Asked session = what_was_asked( s, copy, stdio, name, COPY_USAGE );
    std::string userhost = session.host;
    const bool local = s.flag( "local" );
    const Words ssh = shell_words( s.value( "ssh", "ssh" ) );
    if ( ssh.empty() ) {
      throw Failure( name + ": --ssh needs a command." );
    }

    /*
     * ---- (gosh, REQ19/CMD-5) the host, and the meetup, reached as the user's own ssh would reach
     * them: a ProxyJump or ProxyCommand of theirs is not displaced by ours, and a meetup named by an
     * alias is sent to at its HostName (sshconfig.h)
     */
    if ( !local ) {
      const Gosh::Ssh::SshConfig config = Gosh::Ssh::read_ssh_config( ssh_config_text( ssh, userhost ) );
      choices.remote_ip = Gosh::Ssh::remote_ip_for( s.has( "experimental-remote-ip" ) ? choices.remote_ip : "", config );
    }
    if ( !choices.meetup_login.empty() ) {
      const Gosh::Ssh::SshConfig config = Gosh::Ssh::read_ssh_config( ssh_config_text( ssh, choices.meetup_login ) );
      for ( size_t i = 0; i < choices.relays_read.size(); i++ ) {
        if ( choices.relays_read[i].first == "GOSH_MEETUP" ) {
          choices.relays_read[i].second = Gosh::Ssh::meetup_address( choices.relays_read[i].second, config );
        }
      }
    }
    set_variables( choices.relays_read );
    const Words bind = bind_arguments( s.value( "bind-server" ), local, userhost );

    /* ---- how many colors the terminal has, for the server to know (there is none without one) */

    const std::string colors = session.headless ? "0" : terminal_colors( print_colors, name );

    /* ---- the server's address, if it is for us to find */

    Startup startup;
    if ( choices.remote_ip == "local" ) {
      const size_t at = userhost.rfind( '@' );
      const std::string user = at == std::string::npos ? "" : userhost.substr( 0, at + 1 );
      const std::string host = at == std::string::npos ? userhost : userhost.substr( at + 1 );
      const std::vector<Address> found = resolve( host, "22", choices.family, name );
      if ( found.empty() ) {
        throw Failure( "could not find address for " + host );
      }
      startup.ip = found[0].text;
      userhost = user + startup.ip;
    }

    /* ---- start the server, by ssh, and hear from it */

    const std::string server = s.value( "server", DEFAULT_SERVER );
    Words locales;
    for ( size_t i = 0; i < sizeof( LOCALE_NAMES ) / sizeof( LOCALE_NAMES[0] ); i++ ) {
      if ( const char* value = getenv( LOCALE_NAMES[i] ) ) {
        locales.push_back( std::string( LOCALE_NAMES[i] ) + "=" + value );
      }
    }
    ServerWords server_args = server_words( s, choices, session, colors, bind, locales );
    const bool pick_server = !s.has( "server" );

    Login login;
    login.local = local;
    login.proxy = choices.remote_ip == "proxy";
    login.userhost = userhost;
    login.server_line = server_line( pick_server, server, server_args );
    login.server = server;
    login.pick_server = pick_server;
    login.port_range = s.has( "port" ) && s.value( "port" ).find( ':' ) != std::string::npos;
    login.quiet = !session.stream_local.empty() || session.headless;
    login.name = name;
    if ( !local ) {
      login.words = login_words( ssh, s.flag( "ssh-pty", true ), choices,
                                 login.proxy ? proxy_command( self_path( self ), choices.family ) : "", userhost,
                                 login.server_line );
    }
    start_server( login, startup );

    const std::string note = startup_address( startup, name, userhost );
    if ( !note.empty() ) {
      fprintf( stderr, "%s\n", note.c_str() );
    }
    startup_key( startup, name );

    /*
     * (gosh, REQ8/CMD-3) A stock server was asked for no forward, stream or relay. Say so once,
     * naming each thing the user asked for that will not be done, and do not have the client
     * wait on forwards the server knows nothing of. A stream is the whole of what was asked
     * (a copy, or --stdio), so without one there is nothing to go on with. The host is named
     * as the user gave it (session.host); userhost may by now hold its address.
     */
    if ( startup.stock ) {
      if ( !session.stream_local.empty() ) {
        throw Failure( name + ": " + session.host + " has no gosh-server, and its mosh-server cannot do this ("
                       + ( copy ? "copying files" : "--stdio" ) + ")." );
      }
      Words paths; /* what only gosh's relay does, asked for in words the user wrote */
      if ( !choices.meetup_login.empty() ) {
        paths.push_back( "--meetup=" + s.value( "meetup" ) );
      }
      if ( s.has( "stun" ) && s.value( "stun" ) != "none" ) {
        paths.push_back( "--stun=" + s.value( "stun" ) );
      }
      if ( s.has( "punch" ) && choices.punch != "never" ) {
        paths.push_back( "--punch=" + choices.punch );
      }
      fprintf( stderr, "%s\n",
               stock_notice( name, session.host, server_args.forwards, s.has( "relay" ) && choices.relay != "never",
                             paths )
                 .c_str() );
      server_args.forwards.clear();
    }

    /* ---- become the client */

    set_variables( client_variables( startup.key, choices, s.flag( "init", true ), s.flag( "predict-overwrite" ) ) );
    set_variables( relay_variables( choices,
                                    startup.stock,
                                    relay_command( ssh, local, pick_server, server, userhost, startup.ip, startup.port ),
                                    ticket_command( choices.meetup_login ) ) );

    client_args = client_words( args, server_args.forwards, session, startup.ip, startup.port );
    return true;
  } catch ( const Failure& f ) {
    if ( !f.message.empty() ) {
      fprintf( stderr, "%s\n", f.message.c_str() );
    }
    if ( f.with_usage ) {
      fputs( "\n", stderr );
      if ( copy ) {
        fputs( COPY_USAGE, stderr );
      } else {
        fprintf( stderr, USAGE, name.c_str() );
      }
    }
    return false;
  }
}
