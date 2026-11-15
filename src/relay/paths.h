/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* The rules are the fork's (mosh-1.4.0-pf.7, src/network/relay.cc: LocalRelay's from_client,
   start_child and run, and relay_remote). */

/*
 * relay, paths: which way a datagram goes, and when the ssh connection is started, started
 * again, and let go (REQ1/PATH-1, REQ1/PATH-2, REQ1/SHAPE-2; design v2, 2.2 and 7.1, 2).
 *
 * Logic: times in milliseconds in, decisions out. No socket, no clock, no process. relay.cc
 * is its edge and does what it says.
 *
 * The local relay sends everything from the client straight to the server by UDP, always
 * (unless told to use nothing but the stream). While answers come straight back, that is all.
 * When none has come for 2.5 s, each datagram goes by the stream as well, the stream being
 * started for it; duplicates cost nothing, and there is never a choice to get wrong. After 10 s
 * without an answer it also tries from a fresh socket, every 10 s. A stream not needed for a
 * minute is let go. One that cannot be started is tried again after 1 s, 2 s, 4 s, ... up to
 * 30 s, or up to 5 minutes while it has never once worked (then ssh most likely cannot log in
 * without asking, which will not get better).
 *
 * What it does when an input is absent, late, wrong or lying: an answer that never comes is the
 * case it is for. A stream that starts and says nothing counts as not yet working (the long
 * back-off). Times are the edge's own monotonic clock; nothing here compares them with anyone else's.
 */

#ifndef GOSH_RELAY_PATHS_H
#define GOSH_RELAY_PATHS_H

#include <algorithm>
#include <cstdint>

namespace Gosh {

struct RelayRules
{
  /* how long without an answer by UDP before the stream is brought in */
  static constexpr uint64_t DIRECT_QUIET_MS = 2500;
  /* how long the stream may go unneeded before it is closed */
  static constexpr uint64_t STREAM_IDLE_MS = 60000;
  /* how long without an answer by UDP before trying from a fresh socket */
  static constexpr uint64_t DIRECT_HOP_MS = 10000;
  /* between attempts to start the remote relay: doubling from, up to */
  static constexpr uint64_t RESPAWN_MIN_MS = 1000;
  static constexpr uint64_t RESPAWN_MAX_MS = 30000;
  /* ...and up to this while it has never yet worked */
  static constexpr uint64_t RESPAWN_HOPELESS_MAX_MS = 300000;
  /* how long the remote relay stays, its stream gone, for a way through that has gone quiet */
  static constexpr uint64_t PUNCH_LINGER_MS = 60000;
};

/* The local relay's paths. */
class LocalPaths : public RelayRules
{
private:
  bool stream_only;  /* use the stream and nothing else */
  bool have_command; /* there is something to start the stream with */
  uint64_t stream_idle;

  bool running; /* the stream's process is started */
  uint64_t respawn_after, respawn_delay;
  bool ever_worked;
  uint64_t direct_heard, direct_hopped, stream_wanted;

public:
  /* What to do with a datagram from the client, beyond sending it straight to the server. */
  struct Also
  {
    bool by_stream;    /* send it by the stream too, if the stream is running (after start_stream) */
    bool start_stream; /* first, try to start the stream; then say how it went: stream_started() */
    bool hop;          /* open a fresh socket for the direct way: a new port sometimes gets through */

    Also() : by_stream( false ), start_stream( false ), hop( false ) {}
  };

  LocalPaths( bool s_stream_only, bool s_have_command, uint64_t s_stream_idle = STREAM_IDLE_MS )
    : stream_only( s_stream_only ), have_command( s_have_command ), stream_idle( s_stream_idle ), running( false ),
      respawn_after( 0 ), respawn_delay( RESPAWN_MIN_MS ), ever_worked( false ), direct_heard( 0 ), direct_hopped( 0 ),
      stream_wanted( 0 )
  {}

  /*
   * A datagram has come from the client.
   * have_direct: there is a socket for the direct way.
   * through_heard: when the way through (path-finding) last heard the other relay; 0 if there is none.
   */
  Also from_client( uint64_t now, bool have_direct, uint64_t through_heard )
  {
    Also also;
    /* Is either route by UDP answering?  If not, by the stream as well. */
    const uint64_t udp_heard = std::max( direct_heard, through_heard );
    if ( stream_only || now - udp_heard > DIRECT_QUIET_MS ) {
      stream_wanted = now;
      if ( !running && now >= respawn_after && have_command ) {
        also.start_stream = true;
        respawn_after = now + respawn_delay;
        const uint64_t most = ever_worked ? RESPAWN_MAX_MS : RESPAWN_HOPELESS_MAX_MS;
        respawn_delay = respawn_delay * 2 > most ? most : respawn_delay * 2;
      }
      also.by_stream = true;
      if ( have_direct && now - direct_heard > DIRECT_HOP_MS && now - direct_hopped > DIRECT_HOP_MS ) {
        direct_hopped = now;
        also.hop = true;
      }
    }
    return also;
  }

  /* The stream's process was started, or could not be. */
  void stream_started( bool ok ) { running = ok; }

  /* The stream's process has gone, or was stopped. */
  void stream_stopped( void ) { running = false; }

  /* Something came back by the stream: it works. */
  void stream_answered( void )
  {
    respawn_delay = RESPAWN_MIN_MS;
    ever_worked = true;
  }

  /* An answer came straight from the server. */
  void direct_answered( uint64_t now ) { direct_heard = now; }

  /*
   * Asked each turn: has the direct way been enough for so long that the stream should be let
   * go? If yes, the caller stops it, and it may be started again at once when it is wanted.
   */
  bool let_stream_go( uint64_t now )
  {
    if ( running && !stream_only && now - stream_wanted > stream_idle ) {
      running = false;
      respawn_after = 0;
      return true;
    }
    return false;
  }

  bool stream_running( void ) const { return running; }
};

/* The remote relay: by which ways an answer from the server goes back. */
struct RemoteAnswer
{
  bool through;
  bool stream;
};

/*
 * through_open: the way through is open. stream_usable: the stream is there and has not ended.
 * stream_heard: when the local relay last sent anything by the stream.
 * By the way through if there is one; and by the stream if there is no way through, or the
 * local relay is still using the stream itself (it sent by it within 3 s: its 2.5 s and a margin).
 */
inline RemoteAnswer remote_answer_by( uint64_t now, bool through_open, bool stream_usable, uint64_t stream_heard )
{
  RemoteAnswer by;
  by.through = through_open;
  by.stream = stream_usable && ( !through_open || now - stream_heard < RelayRules::DIRECT_QUIET_MS + 500 );
  return by;
}

/* The remote relay, its stream gone: whether it is finished. (It stays only for a way through.) */
inline bool remote_finished( uint64_t now, uint64_t through_heard, uint64_t stream_closed )
{
  return now - std::max( through_heard, stream_closed ) > RelayRules::PUNCH_LINGER_MS;
}

}

#endif
