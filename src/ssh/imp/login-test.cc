/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ1/CMD-1: ssh's words: the host's shell's (shell.h), each login's (login.h), and the ProxyCommand's (proxy.h). Plain strings. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/ssh/login.h"
#include "src/ssh/proxy.h"
#include "src/ssh/shell.h"
#include "vendor/facts/facts.h"

using namespace Gosh::Ssh;

static std::string joined( const Words& w )
{
  std::string s;
  for ( size_t i = 0; i < w.size(); i++ ) {
    s += ( i ? "|" : "" ) + w[i];
  }
  return s;
}

/* REQ1/CMD-1: ssh's words */
FACTS( Login )
{
  /* ---- words as a shell has them */
  FACT_CHECK( shell_words( " a 'b c'\t\"d \\\" e\" f\\ g \n" ) == Words( { "a", "b c", "d \" e", "f g" } ), "shell_words: quotes and a backslash taken away, blanks between" );
  FACT_CHECK( shell_words( "a '' b" ) == Words( { "a", "", "b" } ), "shell_words: an empty quoted word is a word" );
  FACT_CHECK( shell_quote( { "it's", "a b" } ) == "'it'\\''s' 'a b'", "shell_quote: each word in single quotes, a quote in one closed and opened around it" );
  FACT_CHECK( shell_words( shell_quote( { "it's", "", "$x" } ) ) == Words( { "it's", "", "$x" } ), "and back: shell_words of shell_quote is the words" );

  /* ---- ssh's words, to start the server */
  FACT_CHECK( joined( login_words( { "ssh", "-p", "2" }, true, "proxy", "auto", "PX %h %p", "me@h", "LINE" ) )
                == "ssh|-p|2|-n|-tt|-S|none|-o|ProxyCommand=PX %h %p|me@h|--|LINE", "by proxy: -n -tt, no ControlMaster, this program as the ProxyCommand" );
  const Words rw = login_words( { "ssh" }, false, "remote", "inet6", "", "h", "LINE" );
  FACT_CHECK( ( rw.size() == 6 && joined( Words( rw.begin(), rw.begin() + 5 ) ) == "ssh|-n|-6|h|--" && rw[5].compare( 0, 6, "sh -c " ) == 0
                && rw[5].find( "MOSH SSH_CONNECTION" ) != std::string::npos && rw[5].compare( rw[5].size() - 7, 7, " ; LINE" ) == 0 ),
              "by remote: no -tt without a pty, -6, and the server asked its address first" << ": " << ( joined( rw ) ) );
  FACT_CHECK( joined( login_words( { "ssh" }, true, "local", "inet", "", "h", "LINE" ) ) == "ssh|-n|-tt|h|--|LINE", "by local: ssh is asked nothing more, not even -4" );
  FACT_CHECK( joined( asking_nothing( { "ssh", "-p", "2" }, "me@h", "run this" ) ) == "ssh|-p|2|-T|-o|BatchMode=yes|me@h|--|run this",
              "asking nothing: no terminal, and BatchMode, so ssh asks for no password" );

  /* ---- ssh's ProxyCommand: ssh reads %h and %p in it, so a % in this program's own path is doubled */
  const std::string proxy_line = proxy_command( "/opt/50%off/gosh-client", "inet" );
  FACT_CHECK( proxy_line == "'/opt/50%%off/gosh-client' --mosh-fake-proxy inet %h %p", "the ProxyCommand: this program's path quoted, its % doubled, then %h %p for ssh (the review of item 10)" << ": " << ( proxy_line ) );
  std::vector<Address> found;
  std::string why;
  FACT_CHECK( ( resolve( "127.0.0.1", "22", "auto", found, why ) && found.size() == 1 && found[0].text == "127.0.0.1" ), "an address as numbers is itself, asked of no one" );
  FACT_CHECK( ( !resolve( "127.0.0.1", "22", "inet6", found, why ) && why.compare( 0, 27, "could not resolve 127.0.0.1" ) == 0 ), "and, of another family, not resolved, with why" << ": " << why );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Login );
}

FACTS_MAIN
