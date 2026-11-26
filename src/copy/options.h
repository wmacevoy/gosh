/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * copy, options: the words that say which end of a copy this is and how it behaves (REQ1/CP-1,
 * REQ1/CP-5). Logic. The fork's (mosh-1.4.0-pf.7, src/network/filecopy.cc, Gosh::Copy::parse).
 *
 * The words: "send" or "receive", then any of -r --auto --delete --glob -q --interval=MS, then "--"
 * and the paths. Wrong input: anything else, an interval under 100 ms, no "--", a sender with no
 * source or a receiver without exactly one destination: refused (false).
 */

#ifndef GOSH_COPY_OPTIONS_H
#define GOSH_COPY_OPTIONS_H

#include <cstdlib>
#include <string>
#include <vector>

namespace Gosh::Copy {

struct Options
{
  bool sender;    /* or the receiver */
  bool local;     /* at the user's end: says what happens, on standard error */
  bool recursive; /* directories and what is in them */
  bool automatic; /* keep going, sending what changes */
  bool remove;    /* what is gone from the source goes from the destination */
  bool glob;      /* sender: the paths are patterns, as a shell would take them */
  bool quiet;
  int interval;                   /* ms between looks, when automatic */
  std::vector<std::string> paths; /* the sources, or the one destination */

  Options()
    : sender( false ), local( false ), recursive( false ), automatic( false ), remove( false ), glob( false ),
      quiet( false ), interval( 2000 ), paths()
  {}
};

inline bool parse( const std::vector<std::string>& words, Options& options )
{
  if ( words.empty() || ( words[0] != "send" && words[0] != "receive" ) ) {
    return false;
  }
  options.sender = words[0] == "send";
  size_t i = 1;
  for ( ; i < words.size() && words[i] != "--"; i++ ) {
    const std::string& w = words[i];
    if ( w == "-r" ) {
      options.recursive = true;
    } else if ( w == "--auto" ) {
      options.automatic = true;
    } else if ( w == "--delete" ) {
      options.remove = true;
    } else if ( w == "--glob" ) {
      options.glob = true;
    } else if ( w == "-q" ) {
      options.quiet = true;
    } else if ( w.compare( 0, 11, "--interval=" ) == 0 ) {
      options.interval = atoi( w.c_str() + 11 );
      if ( options.interval < 100 ) {
        return false;
      }
    } else {
      return false;
    }
  }
  if ( i >= words.size() ) {
    return false;
  }
  options.paths.assign( words.begin() + i + 1, words.end() );
  return options.sender ? !options.paths.empty() : options.paths.size() == 1;
}

}

#endif
