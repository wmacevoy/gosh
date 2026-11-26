/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * copy, messages: the thirteen messages of "mocp-1", written and read (REQ1/CP-1). Logic: bytes
 * in, values out. The fork's layout (mosh-1.4.0-pf.7, src/network/filecopy.cc), byte for byte, so
 * that a gosh end copies with a pf.7 end.
 *
 * A message on the stream is a type (one byte), a length (four, most significant first) and that
 * many bytes; numbers in a body are most significant first, and text is a length (four) and its
 * bytes.
 *
 *   sender                               receiver
 *     HELLO "mocp-1", HELLO (many?) -->  <-- HELLO "mocp-1"
 *     LIST (entries) ...            ---->
 *     LIST_END (complete?)          ---->
 *                                   <----   WANT (index, offset, hash of what is held) ...
 *                                   <----   PLAN_END
 *     FILE_BEGIN, DATA..., FILE_END ---->
 *                                   <----   RESULT (index, ok, why)
 *     ROUND_END                     ---->
 *                                   <----   ROUND_DONE
 *     BYE                           ---->
 *   and from either, at any time: NOTE (error?, text), for the user.
 *
 * Wrong input: a body too short for its message, a length over MAX_MESSAGE, a listed entry of
 * another kind than F, D or X, or with a name that is not safe (names.h): Malformed, and the end
 * stops. Bytes after what a body needs are ignored, as pf.7 ignores them.
 */

#ifndef GOSH_COPY_MESSAGES_H
#define GOSH_COPY_MESSAGES_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "src/copy/imp/checksum.h"
#include "src/copy/imp/names.h"

namespace Gosh::Copy::Imp {

enum MessageType
{
  HELLO = 1,
  LIST,
  LIST_END,
  WANT,
  PLAN_END,
  FILE_BEGIN,
  DATA,
  FILE_END,
  RESULT,
  ROUND_END,
  ROUND_DONE,
  NOTE,
  BYE
};

static const char MAGIC[] = "mocp-1";
static const size_t CHUNK = 32768;             /* the most a DATA carries */
static const size_t MAX_MESSAGE = 1024 * 1024; /* a longer one is not read */
static const size_t LIST_BATCH = 64 * 1024;    /* LIST is sent once it is longer than this */

/* The other end made no sense. */
struct Malformed : public std::runtime_error
{
  Malformed() : std::runtime_error( "the other end sent something that could not be understood" ) {}
};

inline void put8( std::string& s, uint8_t v )
{
  s.push_back( static_cast<char>( v ) );
}

inline void put32( std::string& s, uint32_t v )
{
  for ( int shift = 24; shift >= 0; shift -= 8 ) {
    s.push_back( static_cast<char>( ( v >> shift ) & 0xFF ) );
  }
}

inline void put64( std::string& s, uint64_t v )
{
  for ( int shift = 56; shift >= 0; shift -= 8 ) {
    s.push_back( static_cast<char>( ( v >> shift ) & 0xFF ) );
  }
}

inline void put_string( std::string& s, const std::string& v )
{
  put32( s, v.size() );
  s += v;
}

/* Reads a body from its start; Malformed where it runs short. */
class Reader
{
private:
  const std::string& s;
  size_t at;

  void need( size_t n ) const
  {
    if ( s.size() - at < n ) {
      throw Malformed();
    }
  }

public:
  explicit Reader( const std::string& s_s ) : s( s_s ), at( 0 ) {}

  uint8_t u8( void )
  {
    need( 1 );
    return static_cast<uint8_t>( s[at++] );
  }
  uint32_t u32( void )
  {
    uint32_t v = 0;
    for ( int i = 0; i < 4; i++ ) {
      v = ( v << 8 ) | u8();
    }
    return v;
  }
  uint64_t u64( void )
  {
    uint64_t v = 0;
    for ( int i = 0; i < 8; i++ ) {
      v = ( v << 8 ) | u8();
    }
    return v;
  }
  std::string str( void )
  {
    const uint32_t len = u32();
    need( len );
    at += len;
    return s.substr( at - len, len );
  }
  bool done( void ) const { return at == s.size(); }
};

/* A message as it goes on the stream, added to what is waiting to be written. */
inline void frame( std::string& out, uint8_t type, const std::string& body )
{
  put8( out, type );
  put32( out, body.size() );
  out += body;
}

/* The first whole message in what has arrived, taken out of it: false if none is whole yet. */
inline bool unframe( std::string& in, uint8_t& type, std::string& body )
{
  if ( in.size() < 5 ) {
    return false;
  }
  const size_t len = ( static_cast<size_t>( static_cast<uint8_t>( in[1] ) ) << 24 )
                     | ( static_cast<uint8_t>( in[2] ) << 16 ) | ( static_cast<uint8_t>( in[3] ) << 8 )
                     | static_cast<uint8_t>( in[4] );
  if ( len > MAX_MESSAGE ) {
    throw Malformed();
  }
  if ( in.size() < 5 + len ) {
    return false;
  }
  type = static_cast<uint8_t>( in[0] );
  body.assign( in, 5, len );
  in.erase( 0, 5 + len );
  return true;
}

/* One thing listed: what the sender has, or what is gone from it. */
struct Entry
{
  char kind; /* F a file, D a directory, X something that is no longer there */
  std::string path;
  uint64_t size;
  int64_t mtime;
  uint32_t mode;
  bool force;         /* known to have changed, whatever its size and time say */
  long nsec;          /* (sender's own use, not sent: the rest of the time) */
  std::string source; /* (sender's own use, not sent: where it is) */

  Entry() : kind( 'F' ), path(), size( 0 ), mtime( 0 ), mode( 0 ), force( false ), nsec( 0 ), source() {}

  /* the same as it was, as far as the sender can tell */
  bool same( const Entry& x ) const
  {
    return kind == x.kind && size == x.size && mtime == x.mtime && mode == x.mode && nsec == x.nsec;
  }
};

/* LIST: entries, one after another. */
inline void put_listed( std::string& body, const Entry& e )
{
  put8( body, e.kind );
  put8( body, e.force );
  put_string( body, e.path );
  put64( body, e.size );
  put64( body, static_cast<uint64_t>( e.mtime ) );
  put32( body, e.mode );
}

inline std::vector<Entry> read_list( const std::string& body )
{
  std::vector<Entry> ret;
  Reader r( body );
  while ( !r.done() ) {
    Entry e;
    e.kind = r.u8();
    e.force = r.u8();
    e.path = r.str();
    e.size = r.u64();
    e.mtime = static_cast<int64_t>( r.u64() );
    e.mode = r.u32() & 0777;
    if ( ( e.kind != 'F' && e.kind != 'D' && e.kind != 'X' ) || !safe_relative_path( e.path ) ) {
      throw Malformed();
    }
    ret.push_back( e );
  }
  return ret;
}

/* HELLO's second, from the sender; LIST_END. One flag each. */
inline std::string flag_body( bool v )
{
  std::string m;
  put8( m, v );
  return m;
}

inline bool read_flag( const std::string& body )
{
  return Reader( body ).u8();
}

/* WANT: which, from where, and the checksum of what is held up to there. */
struct Want
{
  uint32_t index;
  uint64_t offset;
  Hash prefix;
  Want() : index( 0 ), offset( 0 ), prefix() {}
};

inline std::string want_body( const Want& w )
{
  std::string m;
  put32( m, w.index );
  put64( m, w.offset );
  put64( m, w.prefix.a );
  put64( m, w.prefix.b );
  return m;
}

inline Want read_want( const std::string& body )
{
  Reader r( body );
  Want w;
  w.index = r.u32();
  w.offset = r.u64();
  w.prefix.a = r.u64();
  w.prefix.b = r.u64();
  return w;
}

/* FILE_BEGIN: which, and from where it is sent. */
inline std::string begin_body( uint32_t index, uint64_t offset )
{
  std::string m;
  put32( m, index );
  put64( m, offset );
  return m;
}

inline void read_begin( const std::string& body, uint32_t& index, uint64_t& offset )
{
  Reader r( body );
  index = r.u32();
  offset = r.u64();
}

/* FILE_END: whether the sender read it whole and unchanged, and the checksum of all of it. */
inline std::string end_body( bool ok, const Hash& hash )
{
  std::string m;
  put8( m, ok );
  put64( m, hash.a );
  put64( m, hash.b );
  return m;
}

inline void read_end( const std::string& body, bool& ok, Hash& hash )
{
  Reader r( body );
  ok = r.u8();
  hash.a = r.u64();
  hash.b = r.u64();
}

/* RESULT: which, whether it took its name, and if not why. */
inline std::string result_body( uint32_t index, bool ok, const std::string& why )
{
  std::string m;
  put32( m, index );
  put8( m, ok );
  put_string( m, why );
  return m;
}

inline void read_result( const std::string& body, uint32_t& index, bool& ok, std::string& why )
{
  Reader r( body );
  index = r.u32();
  ok = r.u8();
  why = r.str();
}

/* NOTE: something for the user, and whether it is an error. */
inline std::string note_body( bool error, const std::string& text )
{
  std::string m;
  put8( m, error );
  put_string( m, text );
  return m;
}

inline void read_note( const std::string& body, bool& error, std::string& text )
{
  Reader r( body );
  error = r.u8();
  text = r.str();
}

}

#endif
