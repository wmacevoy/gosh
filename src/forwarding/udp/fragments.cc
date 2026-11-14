/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#include "src/include/config.h"

#include <cmath>
#include <cstring>

#include "src/forwarding/udp/fragments.h"

using namespace Gosh;

/* Pieces are as equal as can be: the first (total % count) of them are one byte longer than the rest. */
size_t DatagramAssembly::piece_len( size_t total, unsigned int count, unsigned int index )
{
  if ( index >= count ) {
    return ( total + count - 1 ) / count; /* parity is as long as the longest piece */
  }
  return total / count + ( index < total % count ? 1 : 0 );
}

size_t DatagramAssembly::piece_offset( size_t total, unsigned int count, unsigned int index )
{
  return index * ( total / count ) + std::min<size_t>( index, total % count );
}

static std::string fragment_header( unsigned int index, unsigned int count, size_t total )
{
  const char header[DatagramAssembly::FRAGMENT_HEADER_LEN]
    = { static_cast<char>( index ), static_cast<char>( count ), static_cast<char>( total >> 8 ),
        static_cast<char>( total & 0xFF ) };
  return std::string( header, sizeof( header ) );
}

static void xor_into( std::string& parity, const std::string& piece )
{
  for ( size_t i = 0; i < piece.size() && i < parity.size(); i++ ) {
    parity[i] ^= piece[i];
  }
}

unsigned int DatagramAssembly::piece_count( size_t total, size_t max_fragment )
{
  if ( max_fragment <= FRAGMENT_HEADER_LEN || total > MAX_DATAGRAM ) {
    return 0;
  }
  const size_t max_piece = max_fragment - FRAGMENT_HEADER_LEN;
  const size_t count = std::max<size_t>( 2, ( total + max_piece - 1 ) / max_piece );
  /* Indexes must be left for parity whether or not it is sent: the receiver allows for it either way. */
  if ( total < count || count + groups( count ) > MAX_FRAGMENTS ) {
    return 0;
  }
  return count;
}

std::vector<std::string> DatagramAssembly::split( const std::string& datagram, size_t max_fragment, bool parity )
{
  std::vector<std::string> ret;
  const size_t total = datagram.size();

  const unsigned int count = piece_count( total, max_fragment );
  if ( count == 0 ) {
    return ret;
  }

  for ( unsigned int i = 0; i < count; i++ ) {
    ret.push_back( fragment_header( i, count, total )
                   + datagram.substr( piece_offset( total, count, i ), piece_len( total, count, i ) ) );
  }

  if ( parity ) {
    for ( unsigned int g = 0; g < groups( count ); g++ ) {
      std::string sum( piece_len( total, count, count ), '\0' );
      for ( unsigned int i = g * PARITY_GROUP; i < count && i < ( g + 1 ) * PARITY_GROUP; i++ ) {
        xor_into( sum, datagram.substr( piece_offset( total, count, i ), piece_len( total, count, i ) ) );
      }
      ret.push_back( fragment_header( count + g, count, total ) + sum );
    }
  }

  return ret;
}

bool DatagramAssembly::index_of( const std::string& fragment, unsigned int& index )
{
  if ( fragment.size() < FRAGMENT_HEADER_LEN ) {
    return false;
  }
  index = static_cast<uint8_t>( fragment[0] );
  return true;
}

DatagramAssembly::Result DatagramAssembly::add( const std::string& fragment,
                                                std::string& datagram,
                                                bool& used_parity )
{
  if ( fragment.size() < FRAGMENT_HEADER_LEN ) {
    return REJECTED;
  }
  const unsigned int index = static_cast<uint8_t>( fragment[0] );
  const unsigned int its_count = static_cast<uint8_t>( fragment[1] );
  const size_t its_total = ( static_cast<uint8_t>( fragment[2] ) << 8 ) | static_cast<uint8_t>( fragment[3] );

  if ( its_count < 2 || its_total < its_count || index >= its_count + groups( its_count )
       || its_count + groups( its_count ) > MAX_FRAGMENTS
       || fragment.size() - FRAGMENT_HEADER_LEN != piece_len( its_total, its_count, index ) ) {
    return REJECTED;
  }

  if ( count == 0 ) {
    count = its_count;
    total = its_total;
    parts.resize( count + groups( count ) );
    have.resize( count + groups( count ), false );
  } else if ( its_count != count || its_total != total ) {
    return REJECTED;
  }

  if ( !have[index] ) {
    parts[index].assign( fragment, FRAGMENT_HEADER_LEN, std::string::npos );
    have[index] = true;
  }

  return rebuild( datagram, used_parity ) ? COMPLETE : INCOMPLETE;
}

bool DatagramAssembly::rebuild( std::string& datagram, bool& used_parity )
{
  /* Is every group whole, or one piece short with its parity in hand? */
  for ( unsigned int g = 0; g < groups( count ); g++ ) {
    unsigned int missing = 0;
    for ( unsigned int i = g * PARITY_GROUP; i < count && i < ( g + 1 ) * PARITY_GROUP; i++ ) {
      missing += have[i] ? 0 : 1;
    }
    if ( missing > 1 || ( missing == 1 && !have[count + g] ) ) {
      return false;
    }
  }

  used_parity = false;
  for ( unsigned int g = 0; g < groups( count ); g++ ) {
    for ( unsigned int lost = g * PARITY_GROUP; lost < count && lost < ( g + 1 ) * PARITY_GROUP; lost++ ) {
      if ( have[lost] ) {
        continue;
      }
      /* the parity, less every piece we do have, is the one we don't */
      std::string piece = parts[count + g];
      for ( unsigned int i = g * PARITY_GROUP; i < count && i < ( g + 1 ) * PARITY_GROUP; i++ ) {
        if ( i != lost ) {
          xor_into( piece, parts[i] );
        }
      }
      piece.resize( piece_len( total, count, lost ) );
      parts[lost] = piece;
      have[lost] = true;
      used_parity = true;
    }
  }

  datagram.clear();
  datagram.reserve( total );
  for ( unsigned int i = 0; i < count; i++ ) {
    datagram += parts[i];
  }
  return true;
}

bool Gosh::parity_worthwhile( double loss, unsigned int fragments )
{
  /* without parity, the datagram arrives only if every fragment does */
  return 1.0 - pow( 1.0 - loss, fragments ) >= PARITY_WORTHWHILE;
}
