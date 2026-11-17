/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * REQ1/PATH-1, REQ1/PATH-2: every change of path the relays make (paths.h), with numbers for
 * time and no socket, process or sleep. The numbers are the fork's rules at mosh-1.4.0-pf.7,
 * written here before the relays' code was made to ask paths.h, so that the rules are pinned by
 * something other than the code that has them (design v2, 6.2, step 5; queue item 7).
 */

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "src/relay/paths.h"
#include "vendor/facts/facts.h"

using Gosh::LocalPaths;



/* The relay's clock is a monotonic one: a large number when the relay starts. */
static const uint64_t T = 5000000;
FACTS( Local )
{
  LocalPaths p( false, true );
  /*
   * The client starts its relay because the server is not answering, so the relay begins as if
   * nothing had been heard for ever: the first datagram goes by the stream too, which is started
   * for it, and from a fresh socket.
   */
  LocalPaths::Also also = p.from_client( T, true, 0 );
  FACT_CHECK( ( also.start_stream && also.by_stream && also.hop ), "the first datagram: the stream is started, it goes by the stream too, and a fresh socket is tried" );
  p.stream_started( true );
  also = p.from_client( T + 100, true, 0 );
  FACT_CHECK( ( also.by_stream && !also.start_stream && !also.hop ), "the next: by the stream too; it is not started twice, nor another socket tried" );

  FACT_CHECK( !p.from_client( T + 1500, true, 0 ).start_stream, "nor is it started again while it runs, the wait after its start long past" );

  p.direct_answered( T + 200 );
  also = p.from_client( T + 300, true, 0 );
  FACT_CHECK( ( !also.by_stream && !also.start_stream && !also.hop ), "an answer came straight back: the next datagram goes direct and nothing more" );
  FACT_CHECK( !p.from_client( T + 2700, true, 0 ).by_stream, "2,500 ms after the last direct answer: still direct alone" );
  FACT_CHECK( p.from_client( T + 2701, true, 0 ).by_stream, "2,501 ms after: by the stream as well" );

  /* a way through (path-finding) that is answering counts as the direct way does */
  FACT_CHECK( !p.from_client( T + 5200, true, T + 4200 ).by_stream, "no direct answer for 5 s, but the way through heard 1 s ago: no stream" );

  FACT_CHECK( !p.from_client( T + 10200, true, 0 ).hop, "10,000 ms after the last direct answer: no fresh socket yet" );
  FACT_CHECK( p.from_client( T + 10201, true, 0 ).hop, "10,001 ms after: a fresh socket" );
  FACT_CHECK( !p.from_client( T + 20201, true, 0 ).hop, "and not again for 10,000 ms" );
  FACT_CHECK( p.from_client( T + 20202, true, 0 ).hop, "but at 10,001" );
  FACT_CHECK( !LocalPaths( true, true ).from_client( T, false, 0 ).hop, "no socket for the direct way (stream only): none is asked for" );

  /* letting the stream go: it was last wanted at T + 20202 */
  p.direct_answered( T + 20300 );
  p.from_client( T + 21000, true, 0 ); /* (the session goes on talking by the direct way, which does not want the stream) */
  p.direct_answered( T + 21010 );
  FACT_CHECK( ( !p.let_stream_go( T + 20202 + 60000 ) && p.stream_running() ), "the stream not needed for 60,000 ms: kept" );
  FACT_CHECK( ( p.let_stream_go( T + 20202 + 60001 ) && !p.stream_running() ), "for 60,001 ms: let go" );
  also = p.from_client( T + 90000, true, 0 );
  FACT_CHECK( ( also.start_stream && also.by_stream ), "and when it is wanted again it is started at once, with no wait" );
}

/* The times at which a stream that keeps failing is started again, given a datagram every 100 ms. */
static std::vector<uint64_t> gaps( LocalPaths& p, uint64_t from, int starts, uint64_t& last )
{
  std::vector<uint64_t> found;
  for ( uint64_t now = from; static_cast<int>( found.size() ) < starts; now += 100 ) {
    if ( p.from_client( now, true, 0 ).start_stream ) {
      found.push_back( now - last );
      last = now;
      p.stream_started( true );
      p.stream_stopped(); /* it started, and went at once */
    }
  }
  return found;
}
FACTS( StartingAgain )
{
  LocalPaths p( false, true );
  uint64_t last = T;
  FACT_CHECK( p.from_client( T, true, 0 ).start_stream, "(the first start)" );
  p.stream_started( false ); /* it could not be started */
  FACT_CHECK( !p.from_client( T + 999, true, 0 ).start_stream, "999 ms after a start that failed: not again" );

  const uint64_t never[11] = { 1000, 2000, 4000, 8000, 16000, 32000, 64000, 128000, 256000, 300000, 300000 };
  FACT_CHECK( gaps( p, T + 1000, 11, last ) == std::vector<uint64_t>( never, never + 11 ), "a stream that has never worked is tried after 1, 2, 4, ... 256 s, then every 5 minutes" );

  /* It answers once, and then goes again and again. */
  p.stream_answered();
  const uint64_t worked[7] = { 1000, 2000, 4000, 8000, 16000, 30000, 30000 };
  /* (the wait already set, 5 minutes, runs out first; from then the delays begin again at a second) */
  uint64_t now = last + 300000;
  FACT_CHECK( !p.from_client( now - 1, true, 0 ).start_stream, "(the wait set before it worked still runs)" );
  FACT_CHECK( p.from_client( now, true, 0 ).start_stream, "(and then it is started)" );
  p.stream_started( true );
  p.stream_stopped();
  last = now;
  FACT_CHECK( gaps( p, now + 100, 7, last ) == std::vector<uint64_t>( worked, worked + 7 ), "one that has worked is tried after 1, 2, 4, 8, 16 s, then every 30 s" );

  /*
   * Let go, the stream may be started again at once, whatever wait its earlier failures had
   * built up. (The stream idle time is 1 s here, as a test of the programs sets it.)
   */
  LocalPaths q( false, true, 1000 );
  uint64_t q_last = T;
  gaps( q, T, 7, q_last ); /* seven starts that came to nothing: the wait has grown to 64 s, then 128 s */
  uint64_t began = q_last + 64000;
  FACT_CHECK( q.from_client( began, true, 0 ).start_stream, "(after seven failures, the eighth start, which holds)" );
  q.stream_started( true );
  q.direct_answered( began + 10 );
  FACT_CHECK( q.let_stream_go( began + 1001 ), "(the direct way answers; a second and a millisecond on, the stream is let go)" );
  FACT_CHECK( q.from_client( began + 2600, true, 0 ).start_stream, "wanted again 2.6 s later, it is started at once, not after the 128 s its failures had built up" );

  /* Asked whether to let go a stream that is not running changes nothing: the wait to start again still holds. */
  LocalPaths w( false, true, 100 );
  w.from_client( T, true, 0 );
  w.stream_started( false );
  FACT_CHECK( !w.let_stream_go( T + 400 ), "a stream that is not running is not let go, however long unneeded" );
  FACT_CHECK( ( !w.from_client( T + 999, true, 0 ).start_stream && w.from_client( T + 1000, true, 0 ).start_stream ), "and the wait to start it again still holds: not at 999 ms, but at 1,000" );

  LocalPaths none( false, false );
  LocalPaths::Also also = none.from_client( T, true, 0 );
  FACT_CHECK( ( !also.start_stream && !none.from_client( T + 999999, true, 0 ).start_stream ), "nothing to start a stream with: none is started, however long" );

  LocalPaths only( true, true );
  only.from_client( T, false, 0 );
  only.stream_started( true );
  only.direct_answered( T + 10 );
  FACT_CHECK( only.from_client( T + 20, false, T + 15 ).by_stream, "the stream and nothing else: every datagram by the stream, whatever is heard" );
  FACT_CHECK( !only.let_stream_go( T + 999999 ), "and it is never let go" );
}
FACTS( Remote )
{
  using Gosh::remote_answer_by;
  Gosh::RemoteAnswer by = remote_answer_by( T, false, true, 0 );
  FACT_CHECK( ( by.stream && !by.through ), "the remote relay, no way through: an answer goes by the stream" );
  by = remote_answer_by( T, true, true, T - 2999 );
  FACT_CHECK( ( by.stream && by.through ), "a way through, and the local relay used the stream 2,999 ms ago: by both" );
  by = remote_answer_by( T, true, true, T - 3000 );
  FACT_CHECK( ( !by.stream && by.through ), "3,000 ms ago: by the way through alone" );
  by = remote_answer_by( T, false, false, T );
  FACT_CHECK( ( !by.stream && !by.through ), "no way through and the stream ended: by neither" );
  by = remote_answer_by( T, true, false, T );
  FACT_CHECK( ( !by.stream && by.through ), "a way through and the stream ended: by the way through" );

  FACT_CHECK( !Gosh::remote_finished( T + 60000, T, T - 500 ), "its stream gone: it stays 60,000 ms after the way through was last heard" );
  FACT_CHECK( Gosh::remote_finished( T + 60001, T, T - 500 ), "and is finished at 60,001" );
  FACT_CHECK( ( !Gosh::remote_finished( T + 60001, T, T + 1 )
                                                                       && Gosh::remote_finished( T + 60002, T, T + 1 ) ), "or 60,001 ms after the stream closed, if that was later" );
}
/* REQ1/PATH-1, REQ1/PATH-2: the relays' paths, with no socket and no clock */
FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Local );
  FACTS_REGISTER( StartingAgain );
  FACTS_REGISTER( Remote );
}

FACTS_MAIN
