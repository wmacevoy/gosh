/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * (gosh) fragments: a datagram cut into pieces with parity, rebuilt; and whether parity is worth it (REQ1/FWD-2).
 * The fork's text at mosh-1.4.0-pf.7, from src/network/udpforward.{h,cc}, moved here
 * under its own name and into gosh's namespace; nothing else changed. Logic: no socket, no clock.
 */

#ifndef GOSH_FRAGMENTS_H
#define GOSH_FRAGMENTS_H

#include <cstdint>
#include <string>
#include <vector>

namespace Gosh::Forwarding::Imp {

/*
 * Fragmentation of one datagram.
 *
 * The datagram is cut into "count" pieces of as nearly equal size as
 * possible (so that parity costs as little as possible).  Each travels
 * with a 4-byte header: its index, the count, and the datagram's total
 * length, from which the receiver knows every piece's size and place.
 *
 * With parity, the pieces are taken in groups of PARITY_GROUP, and each
 * group is followed (at index count + group number) by the exclusive-or
 * of its pieces.  Any one piece of a group can be rebuilt from the
 * others and the parity, so the datagram survives the loss of one
 * fragment per group.
 */
class DatagramAssembly
{
public:
  static constexpr size_t FRAGMENT_HEADER_LEN = 4;
  static constexpr unsigned int PARITY_GROUP = 8;
  static constexpr unsigned int MAX_FRAGMENTS = 255; /* pieces and parity together, so at most 226 pieces */
  static constexpr size_t MAX_DATAGRAM = 65535;

  enum Result
  {
    REJECTED,   /* malformed, or doesn't belong to this datagram */
    INCOMPLETE, /* accepted; more are needed */
    COMPLETE    /* the datagram has been rebuilt */
  };

private:
  unsigned int count; /* 0 until the first fragment arrives */
  size_t total;
  std::vector<std::string> parts; /* pieces, then parity */
  std::vector<bool> have;

  static unsigned int groups( unsigned int count ) { return ( count + PARITY_GROUP - 1 ) / PARITY_GROUP; }
  static size_t piece_len( size_t total, unsigned int count, unsigned int index );
  static size_t piece_offset( size_t total, unsigned int count, unsigned int index );

  bool rebuild( std::string& datagram, bool& used_parity );

public:
  DatagramAssembly() : count( 0 ), total( 0 ), parts(), have() {}

  /* True until a fragment has been accepted. */
  bool empty( void ) const { return count == 0; }

  /*
   * Cut a datagram into fragments (header included) of at most
   * max_fragment bytes each, in the order they should be sent.
   * Returns nothing if that can't be done.
   */
  static std::vector<std::string> split( const std::string& datagram, size_t max_fragment, bool parity );

  /* How many pieces split() would cut a datagram of this length into; 0 if it can't be done. */
  static unsigned int piece_count( size_t total, size_t max_fragment );

  /* The index a fragment claims, which is also how far its sequence number is from the first one's. */
  static bool index_of( const std::string& fragment, unsigned int& index );

  /* On COMPLETE, fills in the datagram and whether parity was needed to rebuild it. */
  Result add( const std::string& fragment, std::string& datagram, bool& used_parity );
};

/* A sender that decides for itself adds parity if the datagram would otherwise be lost at least this often. */
static constexpr double PARITY_WORTHWHILE = 0.02;

/* Would a datagram of this many fragments be lost often enough, at this loss rate, to deserve parity? */
bool parity_worthwhile( double loss, unsigned int fragments );

}

#endif
