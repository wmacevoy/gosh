/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * forwarding, TCP: how long a round trip takes, and from it how long to wait for an
 * acknowledgment before sending again (RFC 6298's arithmetic). Logic: times in ms.
 */

#ifndef GOSH_TCP_TIMER_H
#define GOSH_TCP_TIMER_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Gosh::Forwarding::Imp {

class RoundTrip
{
private:
  bool valid; /* there has been a measurement */
  double srtt, rttvar;

public:
  static constexpr uint32_t RTO_INITIAL = 1000; /* ms */
  static constexpr uint32_t RTO_MIN = 200;
  static constexpr uint32_t RTO_MAX = 30000;

  RoundTrip() : valid( false ), srtt( 0 ), rttvar( 0 ) {}

  /* A segment sent only once was acknowledged this long after it was sent. */
  void sample( double r )
  {
    if ( !valid ) {
      srtt = r;
      rttvar = r / 2;
      valid = true;
    } else {
      rttvar = 0.75 * rttvar + 0.25 * fabs( srtt - r );
      srtt = 0.875 * srtt + 0.125 * r;
    }
  }

  /* The retransmission timeout when nothing has gone wrong: from the measured round trip, if there is one. */
  uint32_t settled( void ) const
  {
    if ( !valid ) {
      return RTO_INITIAL;
    }
    const double rto = srtt + std::max( 4 * rttvar, 10.0 );
    return std::min<double>( std::max<double>( rto, RTO_MIN ), RTO_MAX );
  }
};

}

#endif
