/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* forwarding, UDP: the numbers and words its three pieces share (REQ1/FWD-2). */

#ifndef GOSH_UDP_RULES_H
#define GOSH_UDP_RULES_H

#include <cstddef>
#include <cstdint>

namespace Gosh {

/* What the session can do just now, read by the edge each time. */
struct DatagramLink
{
  bool ready;         /* the other end forwards, and we are connected */
  size_t max_payload; /* the most one side-channel payload may be */
  double loss;        /* the share of datagrams the session is losing, 0 to 1 */
};

struct ConversationRules
{
  enum ParityMode
  {
    PARITY_AUTO, /* when the measured loss makes it worthwhile */
    PARITY_ALWAYS,
    PARITY_NEVER
  };

  /* Datagrams being put back together: how many at once, and for how long. */
  static constexpr size_t MAX_PARTIALS = 64;
  static constexpr uint64_t REASSEMBLY_TIMEOUT = 1000; /* ms */
  static constexpr size_t MAX_REMEMBERED = 256;

  static constexpr size_t MAX_FLOWS = 64;
  static constexpr uint64_t FLOW_IDLE_TIMEOUT = 120000; /* ms */
};

}

#endif
