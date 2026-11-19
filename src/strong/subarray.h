/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * A window on an array, as a type (REQ6/SHAPE-5). Logic.
 *
 * subarray<type, size> is the first `count` elements of an array of `size` of `type`: it is made
 * from the array itself, whose size the compiler knows, and a count that is at most that size. So
 * what it says may be read is there to be read, and whoever takes one owes nothing more than to
 * keep its index below size(): no pointer and length that could say more than is there.
 *
 * Making one expects count <= size, and at( i ) expects i < size(). Both are proved, not tested
 * (the project strong-domain-driven-design), as bounded.h says of `pre`.
 *
 * It is not proved itself: like caller.h it is what the proofs rest on, read by a person. It keeps
 * a pointer, which no proved class may. What makes that sound is the array's own type, and that a
 * subarray does not outlive its array: one is a local or a parameter, never a field, a global or
 * what a function gives back (the proving refuses the program otherwise: "lint").
 *
 * The array may be of another type of the same size (char for unsigned char), read as `type`.
 *
 * Two ways to make one, as for a bounded. subarray, for a proved class: nothing tested.
 * subarray_checked, for everything else: the count is tested when the program runs, and the
 * process ends if it is more than the array, saying who made it (caller.h).
 */

#ifndef GOSH_STRONG_SUBARRAY_H
#define GOSH_STRONG_SUBARRAY_H

#include <cstddef>
#include <cstdint>

#include "src/strong/caller.h"

#ifndef __cpp_contracts
#define pre( condition )
#define GOSH_PRE_IS_NOTHING
#endif

namespace Strong {

template <typename T, size_t N>
class subarray
{
private:
  const T* first;
  uint64_t count;

public:
  template <typename U>
  subarray( U ( &array )[N], uint64_t s_count ) pre( s_count <= N )
    : first( reinterpret_cast<const T*>( array ) ), count( s_count )
  {
    static_assert( sizeof( U ) == sizeof( T ), "an array of another size of element" );
  }

  uint64_t size( void ) const { return count; }

  T at( uint64_t i ) const pre( i < count ) { return first[i]; }
};

template <typename T, size_t N>
class subarray_checked
{
private:
  const T* first;
  uint64_t count;

public:
  template <typename U>
  subarray_checked( U ( &array )[N], uint64_t s_count, caller from = caller() )
    : first( reinterpret_cast<const T*>( array ) ), count( s_count )
  {
    static_assert( sizeof( U ) == sizeof( T ), "an array of another size of element" );
    STRONG_CHECK( s_count <= N, from );
  }

  uint64_t size( void ) const { return count; }

  T at( uint64_t i ) const pre( i < count ) { return first[i]; }
};

}

#ifdef GOSH_PRE_IS_NOTHING
#undef pre
#undef GOSH_PRE_IS_NOTHING
#endif

#endif
