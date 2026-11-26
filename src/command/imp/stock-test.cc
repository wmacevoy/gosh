/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ8/CMD-3: what gosh says when the server it got is a stock mosh's, and the line that picks the server. */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "src/command/imp/stock.h"
#include "vendor/facts/facts.h"
/* REQ8/CMD-3: what is said of a stock server, and the line that picks the server */
FACTS( Stock )
{
  using Words = std::vector<std::string>;

  std::string said = Gosh::Command::Imp::stock_notice( "gosh", "u@host", Words(), false );
  FACT_CHECK( said == "gosh: u@host has no gosh-server, so its mosh-server is used.", "with nothing of gosh's asked for, it says only which server it got" << ": " << ( said ) );

  said = Gosh::Command::Imp::stock_notice( "gosh", "host", { "-L", "8080:h:80", "-F", "always", "-RU", "53:ns:5353" }, false );
  FACT_CHECK( said == "gosh: host has no gosh-server, so its mosh-server is used."
                 " That one cannot do these, and is not asked to: -L 8080:h:80; -RU 53:ns:5353.", "each forward asked for is named as not done; -F is not one" << ": " << ( said ) );

  said = Gosh::Command::Imp::stock_notice( "gosh", "host", { "-R", "1:a:1" }, true );
  FACT_CHECK( said == "gosh: host has no gosh-server, so its mosh-server is used."
                 " That one cannot do these, and is not asked to: -R 1:a:1; the ssh fallback (--relay).", "the ssh fallback, when asked for, is named too" << ": " << ( said ) );

  said = Gosh::Command::Imp::stock_notice( "gosh", "host", Words(), false, { "--meetup=me@m.example:3478", "--stun=s.example:3478" } );
  FACT_CHECK( said == "gosh: host has no gosh-server, so its mosh-server is used."
                 " That one cannot do these, and is not asked to: --meetup=me@m.example:3478; --stun=s.example:3478.", "and the meetup and STUN servers asked for, which a stock server's session never uses (the review of item 10)" << ": " << ( said ) );

  const std::string line = Gosh::Command::Imp::server_command( "'new' '-L' '1:a:1'", "'new'" );
  FACT_CHECK( ( line.compare( 0, 7, "sh -c '" ) == 0 && line[line.size() - 1] == '\'' ), "the host's line is one word for sh -c" << ": " << ( line ) );
  FACT_CHECK( line.find( "exec gosh-server '\\''new'\\'' '\\''-L'\\'' '\\''1:a:1'\\''; fi;" ) != std::string::npos, "it tries gosh-server first, with all the words" << ": " << ( line ) );
  FACT_CHECK( line.find( "\"GOSH SERVER mosh-server\"; exec mosh-server '\\''new'\\'''" ) != std::string::npos, "then says which it is, and starts mosh-server with only its own words" << ": " << ( line ) );
  FACT_CHECK( std::string( Gosh::Command::Imp::STOCK_SERVER_LINE ) == "GOSH SERVER mosh-server", "the stock line is what the host is told to print" );

  const std::string helper = Gosh::Command::Imp::helper_command( "relay '1.2.3.4' '60001'" );
  FACT_CHECK( ( helper.compare( 0, 7, "sh -c '" ) == 0 && helper.find( "$HOME/.local/bin:$PATH" ) != std::string::npos ), "a helper's line (REQ1/PATH-1, REQ15/PATH-4): one word for sh -c, ~/.local/bin first" << ": " << ( helper ) );
  FACT_CHECK( ( helper.find( "exec gosh-server relay '\\''1.2.3.4'\\'' '\\''60001'\\'''" ) != std::string::npos
           && helper.find( "mosh-server" ) == std::string::npos ), "and it runs gosh-server, never mosh-server, with the words" << ": " << ( helper ) );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Stock );
}

FACTS_MAIN
