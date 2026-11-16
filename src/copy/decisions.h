/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * copy, decisions: what each end decides, given what it was told and what stat said (REQ1/CP-2,
 * REQ1/CP-3, REQ1/CP-4, REQ1/CP-5). Logic: values in, a decision out; the ends (filecopy.cc) do the
 * looking, reading and writing. The fork's rules (mosh-1.4.0-pf.7, src/network/filecopy.cc: the
 * middles of Sender::scan, Sender::send_file, Sender::incoming, Receiver::plan_one, Receiver::begin,
 * Receiver::end, Receiver::prune), unchanged.
 */

#ifndef GOSH_COPY_DECISIONS_H
#define GOSH_COPY_DECISIONS_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "src/copy/checksum.h"
#include "src/copy/messages.h"

namespace FileCopy {

/* What a file was found to be, by stat: only what the decisions look at. */
struct Seen
{
  bool exists, directory, regular;
  uint64_t size;
  int64_t mtime;
  long nsec;
  Seen() : exists( false ), directory( false ), regular( false ), size( 0 ), mtime( 0 ), nsec( 0 ) {}
};

/*
 * The sender, each round (REQ1/CP-4, REQ1/CP-5): of what is found now, what to offer, given what
 * the other end was told of before. Something new or not the same as it was is offered; after the
 * first round a file offered again is forced, since its size and time may say it is the same
 * (REQ1/CP-4 does not hold then: it is known to have changed). What was told of and is gone is
 * forgotten, and offered as gone (X) only with remove. known loses what is gone; found's offered
 * entries are marked.
 */
inline std::vector<Entry> changes( std::map<std::string, Entry>& found, std::map<std::string, Entry>& known,
                                   unsigned int round, bool remove )
{
  std::vector<Entry> entries;
  for ( std::map<std::string, Entry>::iterator it = found.begin(); it != found.end(); it++ ) {
    std::map<std::string, Entry>::const_iterator was = known.find( it->first );
    if ( was == known.end() || !was->second.same( it->second ) ) {
      it->second.force = round > 0 && it->second.kind == 'F';
      entries.push_back( it->second );
    }
  }
  std::vector<std::string> gone;
  for ( std::map<std::string, Entry>::const_iterator it = known.begin(); it != known.end(); it++ ) {
    if ( found.find( it->first ) == found.end() ) {
      gone.push_back( it->first );
    }
  }
  for ( size_t i = 0; i < gone.size(); i++ ) {
    known.erase( gone[i] );
    if ( remove ) {
      Entry e;
      e.kind = 'X';
      e.path = gone[i];
      entries.push_back( e );
    }
  }
  return entries;
}

/* The sender: a WANT it can take. Only a file it listed this round, wanted once. */
inline bool want_acceptable( const Want& w, const std::vector<Entry>& entries, bool already_wanted )
{
  return w.index < entries.size() && entries[w.index].kind == 'F' && !already_wanted;
}

/*
 * The sender (REQ1/CP-2): where to send a wanted file from. The receiver holds offset bytes whose
 * checksum is prefix; the sender read done bytes of its own file, whose checksum is read. Only if
 * those are the same bytes does it carry on from there; otherwise from the start.
 */
inline uint64_t send_from( uint64_t offset, uint64_t size, uint64_t done, const Hash& read, const Hash& prefix )
{
  return ( offset <= size && done == offset && read == prefix ) ? offset : 0;
}

/* The sender (REQ1/CP-3): a file read whole is good only if it was not written to meanwhile. */
inline bool unchanged_while_read( const Entry& listed, const Seen& after )
{
  return after.exists && after.size == listed.size && after.mtime == listed.mtime && after.nsec == listed.nsec;
}

/* The receiver: is the destination a place to put things, or the name for the one thing? */
inline bool destination_is_place( bool many, const std::string& given, bool dest_is_directory )
{
  return many || ( !given.empty() && given[given.size() - 1] == '/' ) || dest_is_directory;
}

/* The receiver (REQ1/CP-5): something gone from the source is removed here, never the destination itself. */
inline bool may_remove( bool remove, const std::string& target, const std::string& dest )
{
  return remove && target != dest;
}

enum Holding
{
  WANT_IT,     /* not here, or not the same: ask for it */
  HAVE_IT,     /* here already, the same size and time (REQ1/CP-4) */
  IN_THE_WAY   /* a directory is here under its name */
};

/* The receiver, for a listed file, given what is at its target. */
inline Holding holding( const Entry& listed, const Seen& target )
{
  if ( target.exists ) {
    if ( target.directory ) {
      return IN_THE_WAY;
    }
    if ( !listed.force && target.regular && target.size == listed.size && target.mtime == listed.mtime ) {
      return HAVE_IT;
    }
  }
  return WANT_IT;
}

/* The receiver (REQ1/CP-2): a part file left from before is worth offering as a start. */
inline bool part_counts( const Entry& listed, const Seen& part )
{
  return part.exists && part.regular && part.size > 0 && part.size <= listed.size;
}

/* What the receiver holds for each thing listed this round. */
struct Plan
{
  Entry entry;
  std::string target, part;
  bool wanted;
  uint64_t offset;
  Hash prefix;
  Plan() : entry(), target(), part(), wanted( false ), offset( 0 ), prefix() {}
};

/* The receiver: a FILE_BEGIN it can take. One file at a time, one it asked for, from 0 or where it said. */
inline bool begin_acceptable( bool receiving, uint32_t index, uint64_t offset, const std::vector<Plan>& plans )
{
  return !receiving && index < plans.size() && plans[index].wanted
         && ( offset == 0 || offset == plans[index].offset );
}

enum Arrival
{
  WHOLE,           /* it takes its name */
  SENDER_FAILED,   /* it changed under the sender, or could not be read: what came may yet be a start */
  NOT_AS_SENT,     /* the checksum or the length disagrees: what came is thrown away */
  NOT_WRITTEN      /* it could not be written here */
};

/* The receiver (REQ1/CP-3), at FILE_END: whether what arrived is whole and is what was sent. */
inline Arrival arrival( bool sender_ok, bool written, const Hash& got, const Hash& theirs, uint64_t at,
                        uint64_t size )
{
  if ( !sender_ok ) {
    return SENDER_FAILED;
  }
  if ( !written ) {
    return NOT_WRITTEN;
  }
  if ( !( got == theirs ) || at != size ) {
    return NOT_AS_SENT;
  }
  return WHOLE;
}

/* The receiver (REQ1/CP-5, with remove): what is listed here this round, so that the rest may go. */
inline std::set<std::string> listed_here( const std::vector<Plan>& plans )
{
  std::set<std::string> listed;
  for ( size_t i = 0; i < plans.size(); i++ ) {
    listed.insert( plans[i].target );
    if ( plans[i].wanted ) {
      listed.insert( plans[i].part );
    }
  }
  return listed;
}

}

#endif
