/*
    Copyright 2026 Warren D. MacEvoy

    This file is part of gosh, which is free software under the GNU General
    Public License, version 3 or later (see COPYING). gosh carries mosh's code
    and is not the mosh project's work.
*/

#include "src/command/stock.h"

namespace Gosh {

const char STOCK_SERVER_LINE[] = "GOSH SERVER mosh-server";

/* One word, quoted so that a Bourne shell reads it back as itself. */
static std::string quoted( const std::string& word )
{
  std::string s = "'";
  for ( size_t i = 0; i < word.size(); i++ ) {
    if ( word[i] == '\'' ) {
      s += "'\\''";
    } else {
      s.push_back( word[i] );
    }
  }
  return s + "'";
}

std::string server_command( const std::string& gosh_words, const std::string& stock_words )
{
  const std::string script = "PATH=\"$HOME/.local/bin:$PATH\"; export PATH; "
                             "if command -v gosh-server >/dev/null 2>&1; then exec gosh-server "
                             + gosh_words + "; fi; printf \"\\n%s\\n\" \"" + STOCK_SERVER_LINE + "\"; exec mosh-server "
                             + stock_words;
  return "sh -c " + quoted( script );
}

std::string stock_notice( const std::string& name, const std::string& host, const std::vector<std::string>& forwards,
                          bool relay )
{
  std::string refused;
  for ( size_t i = 0; i + 1 < forwards.size(); i += 2 ) {
    if ( forwards[i] == "-F" ) {
      continue; /* how to protect a forward's datagrams: nothing of its own to refuse */
    }
    refused += ( refused.empty() ? "" : "; " ) + forwards[i] + " " + forwards[i + 1];
  }
  if ( relay ) {
    refused += std::string( refused.empty() ? "" : "; " ) + "the ssh fallback (--relay)";
  }

  std::string notice = name + ": " + host + " has no gosh-server, so its mosh-server is used.";
  if ( !refused.empty() ) {
    notice += " That one cannot do these, and is not asked to: " + refused + ".";
  }
  return notice;
}

}
