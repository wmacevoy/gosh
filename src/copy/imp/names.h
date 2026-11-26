/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * copy, names: whether a name from the other end is safe, where it lands here, the name of its
 * part file, and the name a source goes by at the other end (REQ1/CP-6, REQ1/CP-3). Logic: text in,
 * text out; nothing here looks at the file system. The fork's (mosh-1.4.0-pf.7,
 * src/network/filecopy.cc: safe_relative_path, target_of, part_name, top_name, expand_home).
 *
 * Wrong input: a name that is empty, absolute, holds a NUL, an empty part or "..", or a "." anywhere
 * but first, is not safe, and the receiver stops (Malformed) rather than write anywhere with it.
 */

#ifndef GOSH_COPY_NAMES_H
#define GOSH_COPY_NAMES_H

#include <string>

namespace Gosh::Copy::Imp {

/* For the receiver: a path from the other end that stays inside the destination. */
inline bool safe_relative_path( const std::string& path )
{
  if ( path.empty() || path[0] == '/' || path.find( '\0' ) != std::string::npos ) {
    return false;
  }
  size_t start = 0;
  bool first = true;
  while ( start <= path.size() ) {
    size_t end = path.find( '/', start );
    if ( end == std::string::npos ) {
      end = path.size();
    }
    const std::string part = path.substr( start, end - start );
    /* "." may lead, meaning the destination itself; nothing may climb out */
    if ( part.empty() || part == ".." || ( part == "." && !first ) ) {
      return false;
    }
    first = false;
    start = end + 1;
  }
  return true;
}

/*
 * Where a safe path from the other end goes here. dest: the destination as given, without
 * trailing slashes. into: the destination is a place to put things (or else the name for the one
 * thing being copied).
 */
inline std::string target_of( const std::string& dest, bool into, const std::string& path )
{
  const std::string base = dest == "/" ? "" : dest;
  if ( path == "." ) {
    return dest;
  }
  if ( path.compare( 0, 2, "./" ) == 0 ) {
    return base + path.substr( 1 );
  }
  if ( into ) {
    return base + "/" + path;
  }
  /* the one thing being copied takes the destination's name */
  const size_t slash = path.find( '/' );
  return slash == std::string::npos ? dest : dest + path.substr( slash );
}

/* A name in a directory: no doubled slash when the directory is "/", so that it is the same text
   target_of gives the same thing (found 2026-11-16: --delete into "/" removed what it was given). */
inline std::string child_of( const std::string& dir, const std::string& name )
{
  return dir == "/" ? "/" + name : dir + "/" + name;
}

/* Whether a path's last name is one a part file has (".NAME.mocp-part"): such a file from the other
   end would be taken for NAME's part (found 2026-11-16), so the receiver refuses it (REQ1/CP-3). */
inline bool is_part_name( const std::string& path )
{
  const size_t slash = path.rfind( '/' );
  const std::string last = slash == std::string::npos ? path : path.substr( slash + 1 );
  static const std::string tail = ".mocp-part";
  return last.size() > tail.size() + 1 && last[0] == '.'
         && last.compare( last.size() - tail.size(), tail.size(), tail ) == 0;
}

/* Where a file arrives before it is whole: ".NAME.mocp-part" beside it. */
inline std::string part_name( const std::string& target )
{
  const size_t slash = target.rfind( '/' );
  return slash == std::string::npos ? "." + target + ".mocp-part"
                                    : target.substr( 0, slash + 1 ) + "." + target.substr( slash + 1 ) + ".mocp-part";
}

/* The name a source goes by at the other end: its own, without where it was found. */
inline std::string top_name( std::string source )
{
  while ( source.size() > 1 && source[source.size() - 1] == '/' ) {
    source.erase( source.size() - 1 );
  }
  const size_t slash = source.rfind( '/' );
  const std::string name = slash == std::string::npos ? source : source.substr( slash + 1 );
  /* "the directory I am in", and such: what is in it, not it */
  return ( name.empty() || name == "." || name == ".." ) ? "." : name;
}

/* "~" and "~/..." under home, if there is one; anything else as it is. */
inline std::string expand_home( const std::string& path, const char* home )
{
  if ( home && ( path == "~" || path.compare( 0, 2, "~/" ) == 0 ) ) {
    return std::string( home ) + path.substr( 1 );
  }
  return path;
}

/* The destination as given, without trailing slashes ("/" stays). */
inline std::string without_trailing_slashes( std::string path )
{
  while ( path.size() > 1 && path[path.size() - 1] == '/' ) {
    path.erase( path.size() - 1 );
  }
  return path;
}

}

#endif
