/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */
/* What it does, and some of its words, follow mosh's scripts/mosh.pl (Keith Winstein and others): LICENSE-MOSH. */

#ifndef GOSH_COMMAND_OPTIONS_HPP
#define GOSH_COMMAND_OPTIONS_HPP

#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

/*
 * the command, options: the option table; the command's words read into settings; and what is
 * refused (REQ1/CMD-1; design v2, 2.2). Logic: words in, settings or a Failure out. The words and
 * messages are the fork's (mosh-1.4.0-pf.7, src/frontend/moshwrapper.cc), which are mosh.pl's.
 */
/*
 * (Command: gosh's other parts are in Gosh too, with names such as Session. Two definitions of
 * one name in one program is undefined behaviour, and it was: a crash on the first Failure, found
 * 2026-11-16 by tests/gosh-stock.py.)
 */
namespace Gosh::Command::Imp {

/* What any piece of the command throws: a message for standard error, and whether the usage follows it. */
struct Failure
{
  std::string message;
  bool with_usage;
  explicit Failure( const std::string& s_message, bool s_with_usage = false )
    : message( s_message ), with_usage( s_with_usage )
  {}
};

/* The words, as Getopt::Long took them: -name or --name, a value after "=" or as the next word. */
struct Settings
{
  std::map<std::string, std::vector<std::string> > values; /* by an option's first name */
  std::map<std::string, bool> flags;
  std::vector<std::string> rest;

  Settings() : values(), flags(), rest() {}

  bool flag( const char* name, bool otherwise = false ) const
  {
    std::map<std::string, bool>::const_iterator it = flags.find( name );
    return it == flags.end() ? otherwise : it->second;
  }
  bool has( const char* name ) const { return values.find( name ) != values.end(); }
  std::string value( const char* name, const std::string& otherwise = "" ) const
  {
    std::map<std::string, std::vector<std::string> >::const_iterator it = values.find( name );
    return it == values.end() ? otherwise : it->second.back();
  }
  std::vector<std::string> all( const char* name ) const
  {
    std::map<std::string, std::vector<std::string> >::const_iterator it = values.find( name );
    return it == values.end() ? std::vector<std::string>() : it->second;
  }
};

/*
 * Whether --stdio is among the words before any "--". With --stdio the command stands where ssh
 * would, for a program such as rsync that runs "ssh [-l user] host command -with --options": what
 * follows the host is the command's, so the words are read in order and stop at the host.
 */
bool wants_stdio( const std::vector<std::string>& args );

/*
 * Reads the words into settings. A name may be cut short while it names one option only; a
 * negatable flag may be "no" or "no-" before its name. Throws Failure, with the usage, for an
 * unknown option, a value missing, or a value given to a flag. in_order: stop at the first word
 * that is not an option (and keep a "--" there).
 */
void parse_options( const std::vector<std::string>& args, bool in_order, Settings& settings );

/* What the settings choose, each checked; what is refused throws Failure. */
struct Choices
{
  std::string fragment_parity; /* "", auto, always, never */
  std::string relay;           /* auto, always, never */
  std::string punch;           /* auto, never */
  std::string predict;         /* adaptive, always, never, experimental */
  std::string remote_ip;       /* local, remote, proxy */
  std::string family;          /* inet, inet6, auto, all, prefer-inet, prefer-inet6 */
  std::string meetup_login;    /* (gosh, REQ15/PATH-4) where ssh logs in for the meetup's ticket; "" if none */
  /* the variables the relays read for themselves, set before the server is started: name, value */
  std::vector<std::pair<std::string, std::string> > relays_read;
  Choices()
    : fragment_parity(), relay(), punch(), predict(), remote_ip(), family(), meetup_login(), relays_read()
  {}
};

/*
 * name: what the command calls itself in messages. prediction: MOSH_PREDICTION_DISPLAY, or NULL if
 * it is not set: the prediction mode when no option gives one.
 */
Choices choose( const Settings& settings, const std::string& name, const char* prediction );

/* A server-side UDP port or range, "N" or "N:M", as mosh.pl checked it. Throws Failure. */
void check_port_request( const std::string& request, const std::string& name );

/* ---- text, as every piece of the command needs it */

std::vector<std::string> split( const std::string& s, char at );
std::string join( const std::vector<std::string>& words, const std::string& with );
/* without the line ends and spaces at its end */
std::string strip_end( std::string s );
bool all_digits( const std::string& s );
std::string lower( std::string s );
/* whether word is one of the words in choices, which are separated by spaces */
bool one_of( const std::string& word, const char* choices );

}

#endif
