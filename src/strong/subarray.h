/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * A window on elements that stand one after another, as a type (REQ6/SHAPE-5). Logic.
 *
 * subarray<iterator>( a, n ) is the n elements from a: at( i ) is *( a + i ), for i below n. So
 * whoever takes one owes nothing but to keep its index below size(): no pointer and length handed
 * over apart, to be believed apart.
 *
 * Making one expects what no test can show when the program runs: that those n elements are
 * there, one after another, and that they stay where they are, unchanged in place, for as long as
 * the subarray exists. That is whoever makes it to keep, and it is held before the program runs
 * (the project strong-domain-driven-design): by proof where a proved class makes one; by the
 * lint, which lets one be only a local or a parameter (never a field, a global or what a
 * function gives back), and lets nothing change what it was made from while it is in scope; and
 * where code that is not proved makes one, by that code, each place listed. at( i ) expects
 * i < size(), which is proved of every reader.
 *
 * It is not proved itself: like caller.h it is what the proofs rest on, read by a person. It
 * keeps a pointer, which no proved class may.
 */

#ifndef GOSH_STRONG_SUBARRAY_H
#define GOSH_STRONG_SUBARRAY_H

#include <cstddef>
#include <cstdint>
#include <iterator>

#ifndef __cpp_contracts
#define pre( condition )
#define GOSH_PRE_IS_NOTHING
#endif

namespace Strong {

template <typename Iterator>
class subarray
{
private:
  Iterator first;
  uint64_t count;

public:
  subarray( Iterator a, uint64_t n ) : first( a ), count( n ) {}

  uint64_t size( void ) const { return count; }

  typename std::iterator_traits<Iterator>::value_type at( uint64_t i ) const pre( i < count ) { return *( first + i ); }
};

}

#ifdef GOSH_PRE_IS_NOTHING
#undef pre
#undef GOSH_PRE_IS_NOTHING
#endif

#endif
