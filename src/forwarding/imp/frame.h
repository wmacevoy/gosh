/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * (gosh) frame: the header of a forwarded payload, and the replay window (REQ1/FWD-1, REQ1/FWD-2).
 * The fork's text at mosh-1.4.0-pf.7, from src/network/udpforward.{h,cc}, moved here
 * under its own name and into gosh's namespace; nothing else changed. Logic: no socket, no clock.
 */

#ifndef GOSH_FRAME_H
#define GOSH_FRAME_H

#include <cstdint>
#include <string>
#include <vector>

namespace Gosh::Forwarding::Imp {

/*
 * The frame every forwarded payload travels in on the side channel:
 * magic, type, forward index, flow id (2), sequence number (8).
 */
static constexpr size_t FRAME_HEADER_LEN = 13;
static constexpr uint8_t TYPE_UDP_DATA = 0;
static constexpr uint8_t TYPE_TCP_SEGMENT = 1; /* see tcpforward.h */
static constexpr uint8_t TYPE_UDP_FRAGMENT = 2;
static constexpr uint8_t TYPE_TCP_REVERSE = 3; /* TCP segments of forwards the server listens for (-R) */
static constexpr uint8_t TYPE_UDP_REVERSE_DATA = 4; /* likewise for UDP (-RU) */
static constexpr uint8_t TYPE_UDP_REVERSE_FRAGMENT = 5;
static constexpr uint8_t TYPE_STREAM = 6; /* TCP segments of a stream between a process at each end */

/* The same framing serves every kind of forwarded payload. */
std::string encode_frame( uint8_t type, uint8_t forward, uint16_t flow_id, uint64_t seq, const std::string& data );
bool decode_frame( const std::string& payload,
                   uint8_t& type,
                   uint8_t& forward,
                   uint16_t& flow_id,
                   uint64_t& seq,
                   std::string& data );

/* Sliding-window duplicate suppression, as in IPsec. */
class ReplayWindow
{
public:
  static constexpr uint64_t WINDOW = 1024;

private:
  std::vector<bool> seen; /* seen[ seq % WINDOW ] for highest - WINDOW < seq <= highest */
  uint64_t highest;
  bool any;

public:
  ReplayWindow() : seen( WINDOW, false ), highest( 0 ), any( false ) {}

  /* True the first time a sequence number is offered, unless it is too old to tell. */
  bool accept( uint64_t seq );
};

}

#endif
