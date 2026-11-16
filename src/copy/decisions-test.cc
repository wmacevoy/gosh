/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-2, REQ1/CP-3, REQ1/CP-4, REQ1/CP-5: what each end of a copy decides (decisions.h), with
 * plain values and no file system: what the sender offers each round; where it carries on from;
 * whether what it read changed under it; whether the receiver has a file already, is in the way,
 * or holds a usable start; and whether what arrived is whole and as sent.
 */

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "src/copy/decisions.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

using namespace FileCopy;

static Entry file( const std::string& path, uint64_t size, int64_t mtime )
{
  Entry e;
  e.kind = 'F';
  e.path = path;
  e.size = size;
  e.mtime = mtime;
  e.mode = 0644;
  return e;
}

static Seen regular( uint64_t size, int64_t mtime )
{
  Seen s;
  s.exists = s.regular = true;
  s.size = size;
  s.mtime = mtime;
  return s;
}

int main()
{
  /* the sender's rounds (REQ1/CP-4, REQ1/CP-5) */
  std::map<std::string, Entry> known, found;
  found["a"] = file( "a", 1, 10 );
  found["b"] = file( "b", 2, 20 );
  std::vector<Entry> offered = changes( found, known, 0, false );
  check( "round 0: everything found is offered, none forced", offered.size() == 2 && !offered[0].force && !offered[1].force );
  known["a"] = found["a"];
  known["b"] = found["b"];

  found.clear();
  found["a"] = file( "a", 1, 10 );
  found["b"] = file( "b", 2, 20 );
  check( "later, nothing changed: nothing offered (REQ1/CP-4)", changes( found, known, 1, false ).empty() );

  found["b"] = file( "b", 2, 21 );
  found["c"] = file( "c", 3, 30 );
  offered = changes( found, known, 1, false );
  check( "later, one changed and one new: those two, forced (size and time may say the same)",
         offered.size() == 2 && offered[0].path == "b" && offered[0].force && offered[1].path == "c" && offered[1].force );
  Entry same_but_nsec = found["a"];
  same_but_nsec.nsec = 5;
  found["a"] = same_but_nsec;
  check( "a change below the second is a change", changes( found, known, 1, false ).size() == 3 );

  std::map<std::string, Entry> dir_found;
  Entry dir;
  dir.kind = 'D';
  dir.path = "d";
  dir_found["d"] = dir;
  std::map<std::string, Entry> none;
  check( "a directory offered later is not forced", !changes( dir_found, none, 1, false )[0].force );

  std::map<std::string, Entry> gone_known = known, nothing;
  offered = changes( nothing, gone_known, 1, false );
  check( "gone from the source, without remove: forgotten, not offered", offered.empty() && gone_known.empty() );
  gone_known = known;
  offered = changes( nothing, gone_known, 1, true );
  check( "gone, with remove: offered as gone (X), by name (REQ1/CP-5)",
         offered.size() == 2 && offered[0].kind == 'X' && offered[0].path == "a" && gone_known.empty() );

  /* the sender: WANT, and where to carry on from (REQ1/CP-2) */
  std::vector<Entry> entries;
  entries.push_back( file( "a", 100, 1 ) );
  entries.push_back( dir );
  Want w;
  w.index = 0;
  check( "a WANT for a listed file, not yet wanted: taken", want_acceptable( w, entries, false ) );
  check( "the same file wanted twice: refused", !want_acceptable( w, entries, true ) );
  w.index = 1;
  check( "a WANT for a directory: refused", !want_acceptable( w, entries, false ) );
  w.index = 2;
  check( "a WANT for something not listed: refused", !want_acceptable( w, entries, false ) );

  Hash held;
  held.add( "abc", 3 );
  Hash mine = held;
  check( "the receiver holds the start of this file: carry on from there", send_from( 3, 100, 3, mine, held ) == 3 );
  Hash other;
  other.add( "abd", 3 );
  check( "it holds the start of something else: from the start", send_from( 3, 100, 3, other, held ) == 0 );
  check( "the file is shorter now than what is held: from the start", send_from( 3, 100, 2, mine, held ) == 0 );
  check( "it holds more than the whole file: from the start", send_from( 101, 100, 101, mine, held ) == 0 );

  /* the sender: read unchanged (REQ1/CP-3) */
  const Entry listed = file( "a", 100, 50 );
  Seen after = regular( 100, 50 );
  check( "read whole and unchanged: good", unchanged_while_read( listed, after ) );
  after.size = 101;
  check( "grown while read: not good", !unchanged_while_read( listed, after ) );
  after = regular( 100, 51 );
  check( "touched while read: not good", !unchanged_while_read( listed, after ) );
  after = regular( 100, 50 );
  after.nsec = 1;
  check( "touched within the second: not good", !unchanged_while_read( listed, after ) );
  check( "could not be looked at: not good", !unchanged_while_read( listed, Seen() ) );
  Seen unlooked = regular( 100, 50 );
  unlooked.exists = false;
  check( "could not be looked at, whatever else it says: not good", !unchanged_while_read( listed, unlooked ) );

  /* the receiver: where things go */
  check( "several things: the destination is a place to put them", destination_is_place( true, "d", false ) );
  check( "a trailing slash: a place", destination_is_place( false, "d/", false ) );
  check( "a directory there: a place", destination_is_place( false, "d", true ) );
  check( "one thing, no slash, nothing there: the name for it", !destination_is_place( false, "d", false ) );
  check( "something gone is removed, with remove (REQ1/CP-5)", may_remove( true, "/d/x", "/d" ) );
  check( "never without remove", !may_remove( false, "/d/x", "/d" ) );
  check( "never the destination itself", !may_remove( true, "/d", "/d" ) );

  /* the receiver: has it, wants it, in the way (REQ1/CP-4) */
  check( "nothing there: want it", holding( listed, Seen() ) == WANT_IT );
  check( "the same size and time: have it (REQ1/CP-4)", holding( listed, regular( 100, 50 ) ) == HAVE_IT );
  check( "another size: want it", holding( listed, regular( 99, 50 ) ) == WANT_IT );
  check( "another time: want it", holding( listed, regular( 100, 49 ) ) == WANT_IT );
  Entry forced = listed;
  forced.force = true;
  check( "forced, though size and time agree: want it", holding( forced, regular( 100, 50 ) ) == WANT_IT );
  Seen a_dir;
  a_dir.exists = a_dir.directory = true;
  check( "a directory under its name: in the way", holding( listed, a_dir ) == IN_THE_WAY );
  Seen not_regular;
  not_regular.exists = true;
  not_regular.size = 100;
  not_regular.mtime = 50;
  check( "something else there, of the same size and time: want it", holding( listed, not_regular ) == WANT_IT );

  /* the receiver: a start held from before (REQ1/CP-2) */
  check( "a part file of some of it: a start", part_counts( listed, regular( 40, 0 ) ) );
  check( "a part as long as the file: a start", part_counts( listed, regular( 100, 0 ) ) );
  check( "an empty part, a longer part, or none: no start",
         !part_counts( listed, regular( 0, 0 ) ) && !part_counts( listed, regular( 101, 0 ) )
           && !part_counts( listed, Seen() ) );
  Seen part_dir = a_dir;
  part_dir.size = 40;
  check( "a directory under the part's name: no start", !part_counts( listed, a_dir ) && !part_counts( listed, part_dir ) );

  /* the receiver: FILE_BEGIN */
  std::vector<Plan> plans( 2 );
  plans[0].wanted = true;
  plans[0].offset = 40;
  check( "a file asked for, from where it said or from 0: taken",
         begin_acceptable( false, 0, 40, plans ) && begin_acceptable( false, 0, 0, plans ) );
  check( "from anywhere else: refused", !begin_acceptable( false, 0, 39, plans ) );
  check( "one not asked for, or not listed: refused", !begin_acceptable( false, 1, 0, plans ) && !begin_acceptable( false, 2, 0, plans ) );
  check( "while another is arriving: refused", !begin_acceptable( true, 0, 0, plans ) );

  /* the receiver: FILE_END (REQ1/CP-3) */
  check( "whole, as sent: it takes its name", arrival( true, true, held, held, 100, 100 ) == WHOLE );
  check( "the checksum disagrees: not as sent", arrival( true, true, held, other, 100, 100 ) == NOT_AS_SENT );
  check( "short, with the same checksum: not as sent", arrival( true, true, held, held, 99, 100 ) == NOT_AS_SENT );
  check( "it changed under the sender: that, whatever arrived", arrival( false, true, held, held, 100, 100 ) == SENDER_FAILED );
  check( "it could not be written here: that", arrival( true, false, held, held, 100, 100 ) == NOT_WRITTEN );

  /* the receiver: what is listed, for remove (REQ1/CP-5) */
  plans[0].target = "/d/a";
  plans[0].part = "/d/.a.mocp-part";
  plans[1].target = "/d/b";
  plans[1].part = "/d/.b.mocp-part";
  const std::set<std::string> here = listed_here( plans );
  check( "listed: every target, and the part of one arriving; not another's part",
         here.count( "/d/a" ) && here.count( "/d/.a.mocp-part" ) && here.count( "/d/b" ) && !here.count( "/d/.b.mocp-part" ) );

  if ( failures ) {
    printf( "FAIL REQ1/CP-2, REQ1/CP-3, REQ1/CP-4, REQ1/CP-5: %d checks of the decisions failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/CP-2, REQ1/CP-3, REQ1/CP-4, REQ1/CP-5: the decisions\n" );
  return 0;
}
