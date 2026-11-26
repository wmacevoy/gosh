/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef GOSH_COMMAND_WORDS_HPP
#define GOSH_COMMAND_WORDS_HPP

#include <string>
#include <utility>
#include <vector>

#include "src/command/imp/options.h"

/*
 * the command, what was asked: the settings turned into each program's words (glossary, "words"):
 * the server's, the client's, both ends of a stream, ssh's, the relay's and the meetup ticket's;
 * and which end of a copy is which (REQ1/CMD-1, REQ1/CP-1, REQ1/CP-5, REQ1/CP-7, REQ1/PATH-1,
 * REQ8/CMD-3, REQ15/PATH-4; design v2, 2.2). Logic: words in, words out; the environment and the
 * terminal are read by the caller and passed in. The fork's (mosh-1.4.0-pf.7,
 * src/frontend/moshwrapper.cc, run), divided.
 */
namespace Gosh::Command::Imp {

typedef std::vector<std::string> Words;
typedef std::vector<std::pair<std::string, std::string> > Variables; /* name, value: to be set, in order */

/* ---- words as a shell has them */

/* A line split as a shell would split it into words: quotes, and a backslash, taken away. */
Words shell_words( const std::string& line );
/* Words quoted for a shell, each in single quotes, separated by spaces. */
std::string shell_quote( const Words& words );
/* "[user@]host:path" as a remote place: true, with host and path, if it is one. A colon after a
 * slash, or after a single letter (a Windows drive), is part of a local name. */
bool remote_place( const std::string& word, std::string& host, std::string& path );
/* "HOST:PORT" or "[IPV6]:PORT", as -W takes it: true, with host (brackets taken away) and port, if
 * the host is not empty and the port is a number from 1 to 65535. */
bool host_port( const std::string& word, std::string& host, std::string& port );

/* ---- what was asked: what the session is for */

struct Asked
{
  std::string host;   /* the host, as the user gave it ([user@]host) */
  Words command;      /* what the server is to run; for a session with no terminal, something that waits */
  bool headless;      /* no terminal: -N, a copy or --stdio */
  Words stream_local; /* a stream's two ends (-X): empty if there is none */
  Words stream_remote;
  Asked() : host(), command(), headless( false ), stream_local(), stream_remote() {}
};

/*
 * From the settings: for a copy, which end is which and what each is told (REQ1/CP-1, REQ1/CP-5);
 * for --stdio, the command to run at the other end (REQ1/CP-7); for -W HOST:PORT, the connection to
 * make there (REQ19/FWD-6); otherwise the host and its command.
 * copy: the command was called as gocp, or given --copy. Throws Failure: for a copy, the copy's
 * usage when there are too few words, and a message when neither or both ends are remote or the
 * sources are on two machines; a message for -W with a copy, --stdio, a command, or a HOST:PORT
 * that is not one; the usage when there is no host.
 */
Asked what_was_asked( const Settings& settings, bool copy, bool stdio, const std::string& name,
                        const char* copy_usage );

/* -s or -i ADDRESS for the server, from --bind-server and --local. */
Words bind_arguments( const std::string& bind, bool local, const std::string& userhost );

struct ServerWords
{
  Words gosh;     /* gosh-server's, from "new" to the command */
  Words stock;    /* mosh-server's: the same, less what only gosh-server knows (REQ8/CMD-3) */
  Words forwards; /* the forwarding words, in pairs (-L spec ...; -F parity): the client's as well */
};

/*
 * The server's words. colors: the terminal's number of colours; locales: "NAME=value" for each
 * locale variable that is set, in mosh.pl's order.
 */
ServerWords server_words( const Settings& settings, const Choices& choices, const Asked& session,
                          const std::string& colors, const Words& bind, const Words& locales );

/* The line, for the host's shell, that starts the server: the host's choice (stock.h) unless the
 * user named the server, which is then given all of gosh-server's words. */
std::string server_line( bool pick_server, const std::string& server, const ServerWords& words );

/*
 * ssh's words, to start the server: -n, -tt unless --no-ssh-pty; for --experimental-remote-ip=remote
 * a question for the server's address before the server's line, and -4 or -6; for proxy, this
 * program as ssh's ProxyCommand (proxy: its command line).
 */
Words login_words( const Words& ssh, bool ssh_pty, const Choices& choices, const std::string& proxy,
                   const std::string& userhost, const std::string& server_line );

/*
 * The remote relay's command (REQ1/PATH-1): gosh-server's relay, by ssh with no questions asked, or
 * by a local shell for --local; never mosh-server by name unless the user named the server.
 */
Words relay_command( const Words& ssh, bool local, bool pick_server, const std::string& server,
                     const std::string& userhost, const std::string& ip, const std::string& port );

/* The command that fetches a ticket from the meetup (REQ15/PATH-4): plain ssh to it, for gosh-server's
   meetup-ticket, whatever --ssh and --server are, since those name how to reach another machine and what
   runs there (issue #5). What the meetup's login needs, a port or a key, is in the user's ssh config. */
Words ticket_command( const std::string& meetup_login );

/*
 * The client's words: its name, what was asked (for whoever lists processes), the forwards, the
 * stream or -N, and where the server is.
 */
Words client_words( const Words& args, const Words& forwards, const Asked& session, const std::string& ip,
                    const std::string& port );

/* The variables the client reads: the session's key, prediction, and the terminal's settings. */
Variables client_variables( const std::string& key, const Choices& choices, bool init, bool predict_overwrite );

/*
 * The variables the client's relay reads: how it is started (REQ1/PATH-1) and, with a meetup, how
 * its ticket is fetched (REQ15/PATH-4). With relay never, or a stock server (REQ8/CMD-3), only
 * that there is none.
 */
Variables relay_variables( const Choices& choices, bool stock, const Words& relay, const Words& ticket );

}

#endif
