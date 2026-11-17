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
#include "vendor/facts/facts.h"



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
/* REQ1/CP-2, REQ1/CP-3, REQ1/CP-4, REQ1/CP-5: the decisions */
FACTS( Decisions )
{
  /* the sender's rounds (REQ1/CP-4, REQ1/CP-5) */
  std::map<std::string, Entry> known, found;
  found["a"] = file( "a", 1, 10 );
  found["b"] = file( "b", 2, 20 );
  std::vector<Entry> offered = changes( found, known, 0, false );
  FACT_CHECK( ( offered.size() == 2 && !offered[0].force && !offered[1].force ), "round 0: everything found is offered, none forced" );
  known["a"] = found["a"];
  known["b"] = found["b"];

  found.clear();
  found["a"] = file( "a", 1, 10 );
  found["b"] = file( "b", 2, 20 );
  FACT_CHECK( changes( found, known, 1, false ).empty(), "later, nothing changed: nothing offered (REQ1/CP-4)" );

  found["b"] = file( "b", 2, 21 );
  found["c"] = file( "c", 3, 30 );
  offered = changes( found, known, 1, false );
  FACT_CHECK( ( offered.size() == 2 && offered[0].path == "b" && offered[0].force && offered[1].path == "c" && offered[1].force ), "later, one changed and one new: those two, forced (size and time may say the same)" );
  Entry same_but_nsec = found["a"];
  same_but_nsec.nsec = 5;
  found["a"] = same_but_nsec;
  FACT_CHECK( changes( found, known, 1, false ).size() == 3, "a change below the second is a change" );

  std::map<std::string, Entry> dir_found;
  Entry dir;
  dir.kind = 'D';
  dir.path = "d";
  dir_found["d"] = dir;
  std::map<std::string, Entry> none;
  FACT_CHECK( !changes( dir_found, none, 1, false )[0].force, "a directory offered later is not forced" );

  std::map<std::string, Entry> gone_known = known, nothing;
  offered = changes( nothing, gone_known, 1, false );
  FACT_CHECK( ( offered.empty() && gone_known.empty() ), "gone from the source, without remove: forgotten, not offered" );
  gone_known = known;
  offered = changes( nothing, gone_known, 1, true );
  FACT_CHECK( ( offered.size() == 2 && offered[0].kind == 'X' && offered[0].path == "a" && gone_known.empty() ), "gone, with remove: offered as gone (X), by name (REQ1/CP-5)" );

  /* the sender: WANT, and where to carry on from (REQ1/CP-2) */
  std::vector<Entry> entries;
  entries.push_back( file( "a", 100, 1 ) );
  entries.push_back( dir );
  Want w;
  w.index = 0;
  FACT_CHECK( want_acceptable( w, entries, false ), "a WANT for a listed file, not yet wanted: taken" );
  FACT_CHECK( !want_acceptable( w, entries, true ), "the same file wanted twice: refused" );
  w.index = 1;
  FACT_CHECK( !want_acceptable( w, entries, false ), "a WANT for a directory: refused" );
  w.index = 2;
  FACT_CHECK( !want_acceptable( w, entries, false ), "a WANT for something not listed: refused" );

  Hash held;
  held.add( "abc", 3 );
  Hash mine = held;
  FACT_CHECK( send_from( 3, 100, 3, mine, held ) == 3, "the receiver holds the start of this file: carry on from there" );
  Hash other;
  other.add( "abd", 3 );
  FACT_CHECK( send_from( 3, 100, 3, other, held ) == 0, "it holds the start of something else: from the start" );
  FACT_CHECK( send_from( 3, 100, 2, mine, held ) == 0, "the file is shorter now than what is held: from the start" );
  FACT_CHECK( send_from( 101, 100, 101, mine, held ) == 0, "it holds more than the whole file: from the start" );

  /* the sender: read unchanged (REQ1/CP-3) */
  const Entry listed = file( "a", 100, 50 );
  Seen after = regular( 100, 50 );
  FACT_CHECK( unchanged_while_read( listed, after ), "read whole and unchanged: good" );
  after.size = 101;
  FACT_CHECK( !unchanged_while_read( listed, after ), "grown while read: not good" );
  after = regular( 100, 51 );
  FACT_CHECK( !unchanged_while_read( listed, after ), "touched while read: not good" );
  after = regular( 100, 50 );
  after.nsec = 1;
  FACT_CHECK( !unchanged_while_read( listed, after ), "touched within the second: not good" );
  FACT_CHECK( !unchanged_while_read( listed, Seen() ), "could not be looked at: not good" );
  Seen unlooked = regular( 100, 50 );
  unlooked.exists = false;
  FACT_CHECK( !unchanged_while_read( listed, unlooked ), "could not be looked at, whatever else it says: not good" );

  /* the receiver: where things go */
  FACT_CHECK( destination_is_place( true, "d", false ), "several things: the destination is a place to put them" );
  FACT_CHECK( destination_is_place( false, "d/", false ), "a trailing slash: a place" );
  FACT_CHECK( destination_is_place( false, "d", true ), "a directory there: a place" );
  FACT_CHECK( !destination_is_place( false, "d", false ), "one thing, no slash, nothing there: the name for it" );
  FACT_CHECK( may_remove( true, "/d/x", "/d" ), "something gone is removed, with remove (REQ1/CP-5)" );
  FACT_CHECK( !may_remove( false, "/d/x", "/d" ), "never without remove" );
  FACT_CHECK( !may_remove( true, "/d", "/d" ), "never the destination itself" );

  /* the receiver: has it, wants it, in the way (REQ1/CP-4) */
  FACT_CHECK( holding( listed, Seen() ) == WANT_IT, "nothing there: want it" );
  FACT_CHECK( holding( listed, regular( 100, 50 ) ) == HAVE_IT, "the same size and time: have it (REQ1/CP-4)" );
  FACT_CHECK( holding( listed, regular( 99, 50 ) ) == WANT_IT, "another size: want it" );
  FACT_CHECK( holding( listed, regular( 100, 49 ) ) == WANT_IT, "another time: want it" );
  Entry forced = listed;
  forced.force = true;
  FACT_CHECK( holding( forced, regular( 100, 50 ) ) == WANT_IT, "forced, though size and time agree: want it" );
  Seen a_dir;
  a_dir.exists = a_dir.directory = true;
  FACT_CHECK( holding( listed, a_dir ) == IN_THE_WAY, "a directory under its name: in the way" );
  Seen not_regular;
  not_regular.exists = true;
  not_regular.size = 100;
  not_regular.mtime = 50;
  FACT_CHECK( holding( listed, not_regular ) == WANT_IT, "something else there, of the same size and time: want it" );

  /* the receiver: a start held from before (REQ1/CP-2) */
  FACT_CHECK( part_counts( listed, regular( 40, 0 ) ), "a part file of some of it: a start" );
  FACT_CHECK( part_counts( listed, regular( 100, 0 ) ), "a part as long as the file: a start" );
  FACT_CHECK( ( !part_counts( listed, regular( 0, 0 ) ) && !part_counts( listed, regular( 101, 0 ) )
           && !part_counts( listed, Seen() ) ), "an empty part, a longer part, or none: no start" );
  Seen part_dir = a_dir;
  part_dir.size = 40;
  FACT_CHECK( ( !part_counts( listed, a_dir ) && !part_counts( listed, part_dir ) ), "a directory under the part's name: no start" );

  /* the receiver: FILE_BEGIN */
  std::vector<Plan> plans( 2 );
  plans[0].wanted = true;
  plans[0].offset = 40;
  FACT_CHECK( ( begin_acceptable( false, 0, 40, plans ) && begin_acceptable( false, 0, 0, plans ) ), "a file asked for, from where it said or from 0: taken" );
  FACT_CHECK( !begin_acceptable( false, 0, 39, plans ), "from anywhere else: refused" );
  FACT_CHECK( ( !begin_acceptable( false, 1, 0, plans ) && !begin_acceptable( false, 2, 0, plans ) ), "one not asked for, or not listed: refused" );
  FACT_CHECK( !begin_acceptable( true, 0, 0, plans ), "while another is arriving: refused" );

  /* the receiver: FILE_END (REQ1/CP-3) */
  FACT_CHECK( arrival( true, true, held, held, 100, 100 ) == WHOLE, "whole, as sent: it takes its name" );
  FACT_CHECK( arrival( true, true, held, other, 100, 100 ) == NOT_AS_SENT, "the checksum disagrees: not as sent" );
  FACT_CHECK( arrival( true, true, held, held, 99, 100 ) == NOT_AS_SENT, "short, with the same checksum: not as sent" );
  FACT_CHECK( arrival( false, true, held, held, 100, 100 ) == SENDER_FAILED, "it changed under the sender: that, whatever arrived" );
  FACT_CHECK( arrival( true, false, held, held, 100, 100 ) == NOT_WRITTEN, "it could not be written here: that" );

  /* the receiver: what is listed, for remove (REQ1/CP-5) */
  plans[0].target = "/d/a";
  plans[0].part = "/d/.a.mocp-part";
  plans[1].target = "/d/b";
  plans[1].part = "/d/.b.mocp-part";
  const std::set<std::string> here = listed_here( plans );
  FACT_CHECK( ( here.count( "/d/a" ) && here.count( "/d/.a.mocp-part" ) && here.count( "/d/b" ) && !here.count( "/d/.b.mocp-part" ) ), "listed: every target, and the part of one arriving; not another's part" );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Decisions );
}

FACTS_MAIN
