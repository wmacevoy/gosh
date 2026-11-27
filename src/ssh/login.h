/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef GOSH_SSH_LOGIN_H
#define GOSH_SSH_LOGIN_H

#include <string>

#include "src/ssh/shell.h"

/*
 * ssh, logging in: ssh's words for each login gosh makes, and ssh run (REQ1/CMD-1, REQ19/CMD-5).
 * The words are logic; config_text and exec are edges: a process, its pipes and the environment.
 * From the command's words.cc and login.cc, moved.
 */
namespace Gosh::Ssh {

/*
 * ssh's words to start the server: -n, and -tt if pty; then by remote_ip (--experimental-remote-ip),
 * "remote": -4 or -6 for family "inet" or "inet6", and the server asked its address
 * (SSH_CONNECTION) before line; "proxy": no ControlMaster, and proxy (proxy.h, proxy_command) as
 * ssh's ProxyCommand; anything else ("local"): nothing more. Then userhost, "--" and the line.
 */
Words login_words( const Words& ssh, bool pty, const std::string& remote_ip, const std::string& family,
                   const std::string& proxy, const std::string& userhost, const std::string& line );

/* ssh's words to run `remote` on userhost when nothing may be asked (the terminal is the session's
   by then, or there is none): -T and BatchMode=yes. */
Words asking_nothing( const Words& ssh, const std::string& userhost, const std::string& remote );

/*
 * What the user's ssh would do for this host: the output of `SSH... -G HOST`, which reads the
 * config and connects nowhere (REQ19/CMD-5; config.h reads it). Empty if ssh has no -G (older
 * than 6.8), or says nothing, or fails. ssh's standard input is nothing and its standard error is
 * dropped: a config gosh cannot read is a config gosh does as before with.
 */
std::string config_text( const Words& ssh, const std::string& host );

/*
 * In a process that is to become ssh, with these words (login_words'): it does not return. proxy:
 * ssh's ProxyCommand is this program, so SHELL is /bin/sh first (unusual shells, and what they are
 * told to do at start, break a ProxyCommand mysteriously). If ssh cannot be run: why, on standard
 * output, and exit status 1.
 */
[[noreturn]] void exec( const Words& words, bool proxy );

}

#endif
