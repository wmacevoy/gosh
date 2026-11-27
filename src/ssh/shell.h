/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef GOSH_SSH_SHELL_H
#define GOSH_SSH_SHELL_H

#include <string>
#include <vector>

/*
 * ssh, the host's shell: what ssh runs on a host is one line, which the host's shell splits into
 * words; and what the host says back is lines (REQ1/CMD-1). Logic: words in, words out. From the
 * command's words.cc, moved.
 */
namespace Gosh::Ssh {

typedef std::vector<std::string> Words;

/* A line split as a shell would split it into words: quotes, and a backslash, taken away. */
Words shell_words( const std::string& line );

/* Words quoted for a shell, each in single quotes, separated by spaces: shell_words of it is the words. */
std::string shell_quote( const Words& words );

}

#endif
