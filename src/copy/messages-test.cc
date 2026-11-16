/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/CP-1, REQ1/CP-6: the messages of mocp-1 (messages.h). Each is written byte for byte as pf.7
 * writes it (the bytes are spelled out here, from pf.7's put8/put32/put64/put_string), and reads
 * back to what was written; one that runs short, or is too long, or lists a name that is not safe
 * or a kind that is none of F, D and X, is Malformed.
 */

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "src/copy/messages.h"

static int failures = 0;

static void check( const char* what, bool ok )
{
  printf( "%-92s %s\n", what, ok ? "ok" : "FAIL" );
  if ( !ok ) {
    failures++;
  }
}

static std::string bytes( const char* s, size_t n )
{
  return std::string( s, n );
}

template<typename F>
static bool malformed( F f )
{
  try {
    f();
  } catch ( const FileCopy::Malformed& ) {
    return true;
  }
  return false;
}

static void read_short_want( void )
{
  FileCopy::read_want( std::string( 27, '\0' ) );
}
static void read_short_result( void )
{
  uint32_t i;
  bool ok;
  std::string why;
  FileCopy::read_result( bytes( "\0\0\0\1\1\0\0\0\5ab", 11 ), i, ok, why );
}

int main()
{
  using namespace FileCopy;

  check( "the magic is \"mocp-1\", and the types are 1 to 13 in pf.7's order",
         std::string( MAGIC ) == "mocp-1" && HELLO == 1 && LIST == 2 && WANT == 4 && FILE_BEGIN == 6 && DATA == 7
           && FILE_END == 8 && RESULT == 9 && NOTE == 12 && BYE == 13 );

  std::string out;
  frame( out, DATA, "xyz" );
  check( "on the stream: the type, a 4-byte length, the body (REQ1/CP-1)", out == bytes( "\7\0\0\0\3xyz", 8 ) );
  frame( out, BYE, "" );
  uint8_t type = 0;
  std::string body;
  check( "two in a row come off one at a time",
         unframe( out, type, body ) && type == DATA && body == "xyz" && unframe( out, type, body ) && type == BYE
           && body.empty() && out.empty() );
  std::string partial = bytes( "\7\0\0\0\3xy", 7 );
  check( "one not yet whole stays where it is", !unframe( partial, type, body ) && partial.size() == 7 );
  std::string four = bytes( "\7\0\0\0", 4 );
  check( "nor is a header of four bytes read", !unframe( four, type, body ) );
  std::string huge = bytes( "\7\0\x10\0\1", 5 ); /* 1 MB and one byte */
  check( "a length over 1 MB is Malformed at once", malformed( [&] { unframe( huge, type, body ); } ) );
  std::string most = bytes( "\7\0\x10\0\0", 5 ); /* exactly 1 MB: waits for the rest */
  check( "exactly 1 MB is allowed, and waits for its bytes", !unframe( most, type, body ) );

  Entry e;
  e.kind = 'F';
  e.force = true;
  e.path = "a/b";
  e.size = 0x0102030405060708ULL;
  e.mtime = 0x1122334455667788LL;
  e.mode = 0644;
  e.nsec = 99;
  e.source = "/not/sent";
  std::string list;
  put_listed( list, e );
  check( "a listed entry: kind, force, name, size, time, mode; and not the sender's own fields",
         list
           == bytes( "F\1\0\0\0\3a/b\1\2\3\4\5\6\7\x8\x11\x22\x33\x44\x55\x66\x77\x88\0\0\1\xa4", 2 + 4 + 3 + 8 + 8 + 4 ) );
  Entry d;
  d.kind = 'D';
  d.path = "a";
  d.mode = 0755;
  put_listed( list, d );
  const std::vector<Entry> back = read_list( list );
  check( "a LIST of two reads back to both",
         back.size() == 2 && back[0].kind == 'F' && back[0].force && back[0].path == "a/b" && back[0].size == e.size
           && back[0].mtime == e.mtime && back[0].mode == 0644 && back[1].kind == 'D' && back[1].path == "a"
           && back[1].source.empty() );
  std::string mode_high;
  e.mode = 0104755; /* more than the permissions */
  put_listed( mode_high, e );
  check( "a mode read keeps only the permissions", read_list( mode_high )[0].mode == 0755 );
  e.mode = 0644;

  Entry bad = e;
  bad.kind = 'L';
  std::string other_kind;
  put_listed( other_kind, bad );
  check( "a kind that is none of F, D, X: Malformed", malformed( [&] { read_list( other_kind ); } ) );
  const char* unsafe[] = { "../x", "/etc/passwd", "a/../../b", "", "a//b" };
  bool all_refused = true;
  for ( size_t i = 0; i < sizeof( unsafe ) / sizeof( unsafe[0] ); i++ ) {
    bad = e;
    bad.path = unsafe[i];
    std::string m;
    put_listed( m, bad );
    all_refused = all_refused && malformed( [&] { read_list( m ); } );
  }
  check( "a listed name that would land outside the destination: Malformed (REQ1/CP-6)", all_refused );
  std::string cut = list.substr( 0, list.size() - 1 );
  check( "a LIST that runs short: Malformed", malformed( [&] { read_list( cut ); } ) );
  std::string long_name = bytes( "F\0\xff\xff\xff\xff", 6 );
  check( "a name longer than what is left: Malformed", malformed( [&] { read_list( long_name ); } ) );

  Want w;
  w.index = 7;
  w.offset = 123456;
  w.prefix.a = 1;
  w.prefix.b = 2;
  const std::string wb = want_body( w );
  check( "WANT: index, offset, and the checksum's two halves (28 bytes)",
         wb.size() == 28 && wb.substr( 0, 4 ) == bytes( "\0\0\0\7", 4 ) && wb.substr( 4, 8 ) == bytes( "\0\0\0\0\0\1\xe2\x40", 8 ) );
  const Want wr = read_want( wb );
  check( "and reads back", wr.index == 7 && wr.offset == 123456 && wr.prefix.a == 1 && wr.prefix.b == 2 );
  check( "a WANT that runs short: Malformed", malformed( read_short_want ) );

  uint32_t index = 0;
  uint64_t offset = 0;
  read_begin( begin_body( 3, 99 ), index, offset );
  check( "FILE_BEGIN: index and offset, 12 bytes", begin_body( 3, 99 ).size() == 12 && index == 3 && offset == 99 );

  bool ok = false;
  Hash h;
  Hash sent;
  sent.add( "x", 1 );
  read_end( end_body( true, sent ), ok, h );
  check( "FILE_END: whether it was read whole, and the checksum, 17 bytes",
         end_body( true, sent ).size() == 17 && end_body( true, sent )[0] == 1 && ok && h == sent );

  std::string why;
  read_result( result_body( 5, false, "no room" ), index, ok, why );
  check( "RESULT: index, whether it took its name, why",
         result_body( 5, false, "no room" ) == bytes( "\0\0\0\5\0\0\0\0\7no room", 16 ) && index == 5 && !ok
           && why == "no room" );
  check( "a RESULT whose reason runs short: Malformed", malformed( read_short_result ) );

  bool error = false;
  std::string text;
  read_note( note_body( true, "hi" ), error, text );
  check( "NOTE: whether it is an error, and the text", note_body( true, "hi" ) == bytes( "\1\0\0\0\2hi", 7 ) && error && text == "hi" );
  check( "a flag: one byte, either way", flag_body( true ) == "\1" && !read_flag( flag_body( false ) ) );
  check( "a flag that is not there: Malformed", malformed( [] { read_flag( "" ); } ) );

  if ( failures ) {
    printf( "FAIL REQ1/CP-1, REQ1/CP-6: %d checks of the messages failed\n", failures );
    return 1;
  }
  printf( "PASS REQ1/CP-1, REQ1/CP-6: the messages of mocp-1\n" );
  return 0;
}
