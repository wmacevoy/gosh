/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#include "src/ssh/shell.h"

Gosh::Ssh::Words Gosh::Ssh::shell_words( const std::string& line )
{
  Words words;
  std::string word;
  bool in_word = false;
  char quote = 0;
  for ( size_t i = 0; i < line.size(); i++ ) {
    const char c = line[i];
    if ( quote ) {
      if ( c == quote ) {
        quote = 0;
      } else if ( c == '\\' && quote == '"' && i + 1 < line.size() ) {
        word.push_back( line[++i] );
      } else {
        word.push_back( c );
      }
    } else if ( c == '\'' || c == '"' ) {
      quote = c;
      in_word = true;
    } else if ( c == '\\' && i + 1 < line.size() ) {
      word.push_back( line[++i] );
      in_word = true;
    } else if ( c == ' ' || c == '\t' || c == '\n' ) {
      if ( in_word ) {
        words.push_back( word );
        word.clear();
        in_word = false;
      }
    } else {
      word.push_back( c );
      in_word = true;
    }
  }
  if ( in_word ) {
    words.push_back( word );
  }
  return words;
}

std::string Gosh::Ssh::shell_quote( const Words& words )
{
  std::string s;
  for ( size_t i = 0; i < words.size(); i++ ) {
    s += i ? " '" : "'";
    for ( size_t j = 0; j < words[i].size(); j++ ) {
      if ( words[i][j] == '\'' ) {
        s += "'\\''";
      } else {
        s.push_back( words[i][j] );
      }
    }
    s += "'";
  }
  return s;
}
