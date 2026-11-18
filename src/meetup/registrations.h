/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, registrations: what a meetup does with REGISTER and PASS (REQ18/PATH-5; the proposal,
 * revision 10, section 4). Logic: a datagram, where it came from and the time in; what to send,
 * and to where, out. No socket, no clock, no file: the ticket key is asked of the caller.
 *
 * For each ticket it holds the two ends: where each registered from, when, and which counters it
 * has used. An end registers by sealing, under the ticket key, the address it believes it is at;
 * it is taken only if that is where the datagram came from, and only with a counter it has not
 * used. A PASS is taken only from where its end registered, with a counter not used, and goes on
 * to the ticket's other end. So a datagram copied off the network moves no registration and is
 * passed on no second time (the fork's meetup took whoever last named a pair as its end).
 *
 * Even once the meetup has let a ticket go (REQ18/PATH-5): a REGISTER carries its time by the
 * meetup's clock, and one more than CLOCK_SLACK from it is not taken. An end is let go only
 * END_IDLE (twice CLOCK_SLACK) after its last REGISTER was taken, so by then every REGISTER it was
 * taken with is too old to be taken again. (Not closed: a meetup just started remembers nothing,
 * so for CLOCK_SLACK it would take again a REGISTER copied in the CLOCK_SLACK before it started;
 * README.) The first REGISTER of an end held afresh
 * is the lowest counter taken from it, so no PASS sealed before it is passed. And a full table
 * takes no new ticket, rather than let go of one that may still be replayed. A REGISTER whose time
 * is too far off, but which is otherwise right, is answered with TIME: the meetup's own time,
 * sealed, so that an end with a wrong clock can register by the meetup's.
 *
 * Absent: the other end not registered, or not heard for two minutes: nothing is passed. Late: an
 * end that has not registered again for two minutes is dropped; a ticket past its time is refused
 * and dropped. Wrong: anything that is not a message at its exact length, or whose seal does not
 * open as its kind, direction and end: dropped, unanswered. Lying: a REGISTER from anywhere but
 * the address sealed in it, or a counter used already: dropped. MAX_TICKETS tickets held, each with
 * an end registered within END_IDLE: a new one is not taken until one goes quiet. Nothing is
 * answered to a ticket the meetup did not make, and nothing for a PASS costs a key computation.
 */

#ifndef GOSH_MEETUP_REGISTRATIONS_H
#define GOSH_MEETUP_REGISTRATIONS_H

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "src/crypto/crypto.h"
#include "src/meetup/counter.h"
#include "src/meetup/messages.h"
#include "src/meetup/ticket.h"

namespace Gosh {

/* Which counters an end has used, of the last WINDOW: the highest, and a mark for each below it.
 * The first counter taken is the lowest ever taken: nothing sealed before it is. */
class CounterWindow
{
public:
  static const uint64_t WINDOW = 1024;

private:
  bool any;
  uint64_t top;
  uint64_t seen[WINDOW / 64];

  bool test( uint64_t c ) const { return ( seen[( c % WINDOW ) / 64] >> ( c % 64 ) ) & 1; }
  void set( uint64_t c ) { seen[( c % WINDOW ) / 64] |= uint64_t( 1 ) << ( c % 64 ); }
  void clear( uint64_t c ) { seen[( c % WINDOW ) / 64] &= ~( uint64_t( 1 ) << ( c % 64 ) ); }

public:
  CounterWindow() : any( false ), top( 0 ), seen() {}

  /* True, and the counter marked used, if it has not been used and is not too far behind. What
     it is given is at most 2^62 - 1: the type says so (counter.h), so `top + 1` and `i` below
     never pass the end of 64 bits. */
  bool take( MeetupCounter counter )
  {
    const uint64_t c = counter.number();
    if ( !any ) {
      any = true;
      top = c;
      memset( seen, 0xff, sizeof( seen ) ); /* everything behind it, as if used */
      return true;
    }
    if ( c > top ) {
      for ( uint64_t i = top + 1; i <= c && i - top <= WINDOW; i++ ) {
        clear( i );
      }
      top = c;
      set( c );
      return true;
    }
    if ( top - c >= WINDOW || test( c ) ) {
      return false;
    }
    set( c );
    return true;
  }

  /* The same, for code that is not proved: any number, tested when it was made (counter.h). False,
     and nothing marked, if it was not a counter at all. */
  bool take_checked( MeetupCounterChecked given )
  {
    if ( !given.ok() ) {
      return false;
    }
    MeetupCounter counter( given.number() );
    return take( counter );
  }

  /* The highest counter taken is a counter: at most 2^62 - 1. True only because of what take() is
     given; so `top + 1` above is never past the end of 64 bits. */
  bool invariant() const { return !any || top <= MEETUP_COUNTER_MOST; }
};

class Registrations
{
public:
  static const size_t MAX_TICKETS = 64;
  static const uint64_t CLOCK_SLACK = 60;           /* seconds a REGISTER's time may be from the meetup's */
  static const uint64_t END_IDLE = 2 * CLOCK_SLACK; /* seconds an end is kept without registering again:
                                                       at least twice CLOCK_SLACK, or a REGISTER could be
                                                       taken again once its end is let go */

  /* What to do with a datagram: nothing; or send these bytes to this address ("where"). */
  struct Send
  {
    bool any;
    std::string to;
    std::string bytes;
    Send() : any( false ), to(), bytes() {}
  };

private:
  struct End
  {
    std::string where; /* empty: not registered */
    uint64_t registered;
    CounterWindow counters;
    End() : where(), registered( 0 ), counters() {}
  };
  struct Entry
  {
    std::unique_ptr<Crypto::Session> session;
    End ends[2];
    Entry() : session(), ends() {}
  };
  std::map<std::string, Entry> entries;
  /* The counter of what the meetup seals, for every ticket: from the time it started, in ms, times
   * 1024, so that it is never used twice under one key, across tickets let go and taken again and
   * across restarts (until 2026-11-17 each ticket taken counted from 0 again). */
  uint64_t sealed;

  bool live( const End& e, uint64_t now ) const { return !e.where.empty() && now - e.registered <= END_IDLE; }

  void forget_stale( uint64_t now )
  {
    for ( std::map<std::string, Entry>::iterator i = entries.begin(); i != entries.end(); ) {
      const bool expired = !ticket_good_at( i->first, now );
      const bool idle = !live( i->second.ends[0], now ) && !live( i->second.ends[1], now );
      if ( expired || idle ) {
        entries.erase( i++ );
      } else {
        i++;
      }
    }
  }

public:
  /* started: when the meetup started, UTC seconds. */
  explicit Registrations( uint64_t started ) : entries(), sealed( ( started * 1000 ) << 10 ) {}

  /*
   * A datagram has arrived.
   *   data, n   the datagram
   *   from      where it came from ("where": meetup_where_v4)
   *   now       the time, UTC seconds
   *   key_of    called with a ticket id, gives its key (ticket_key under the master key)
   */
  template<class KeyOf>
  Send datagram( const char* data, size_t n, const std::string& from, uint64_t now, KeyOf key_of )
  {
    Send nothing;
    MeetupHeader h;
    if ( !meetup_read_header( data, n, h ) || ( h.kind != MEETUP_REGISTER && h.kind != MEETUP_PASS ) ) {
      return nothing;
    }
    if ( !ticket_good_at( h.ticket, now ) ) {
      return nothing;
    }
    forget_stale( now );
    std::map<std::string, Entry>::iterator found = entries.find( h.ticket );

    if ( h.kind == MEETUP_PASS ) {
      /* Nothing for a ticket or end the meetup does not hold, or from anywhere but where it registered. */
      if ( found == entries.end() ) {
        return nothing;
      }
      Entry& entry = found->second;
      End& self = entry.ends[h.end];
      if ( !live( self, now ) || self.where != from ) {
        return nothing;
      }
      uint64_t counter;
      std::string datagram;
      if ( !meetup_open( *entry.session, h, false, counter, datagram ) || datagram.empty() || !self.counters.take_checked( MeetupCounterChecked( counter ) ) ) {
        return nothing;
      }
      const End& other = entry.ends[1 - h.end];
      if ( !live( other, now ) ) {
        return nothing;
      }
      Send s;
      s.any = true;
      s.to = other.where;
      s.bytes = meetup_passed( datagram );
      return s;
    }

    /* REGISTER. A ticket not yet held costs one key computation and one open, and is held only if it opens. */
    std::unique_ptr<Crypto::Session> fresh;
    Crypto::Session* session;
    if ( found != entries.end() ) {
      session = found->second.session.get();
    } else {
      const std::string key = key_of( h.ticket );
      if ( key.size() != TICKET_KEY_LEN ) {
        return nothing;
      }
      fresh.reset( new Crypto::Session( raw_key( key ) ) );
      session = fresh.get();
    }
    uint64_t counter, time_ms;
    std::string text, believed;
    if ( !meetup_open( *session, h, false, counter, text ) || !meetup_read_register( text, believed, time_ms ) || believed != from ) {
      return nothing;
    }
    const uint64_t time_s = time_ms / 1000;
    if ( time_s + CLOCK_SLACK < now || time_s > now + CLOCK_SLACK ) {
      Send s; /* sealed by an end with the key, from where it says: tell it the time */
      s.any = true;
      s.to = from;
      s.bytes = meetup_seal( *session, MEETUP_TIME, h.ticket, h.end, true, sealed++, meetup_time_text( counter, now * 1000 ) );
      return s;
    }
    if ( found == entries.end() ) {
      if ( entries.size() >= MAX_TICKETS ) {
        return nothing;
      }
      found = entries.insert( std::make_pair( h.ticket, Entry() ) ).first;
      found->second.session = std::move( fresh );
    }
    Entry& entry = found->second;
    End& self = entry.ends[h.end];
    if ( !self.counters.take_checked( MeetupCounterChecked( counter ) ) ) {
      return nothing;
    }
    self.where = from;
    self.registered = now;
    Send s;
    s.any = true;
    s.to = from;
    s.bytes = meetup_seal( *entry.session, MEETUP_REGISTERED, h.ticket, h.end, true, sealed++, from );
    return s;
  }

  size_t ticket_count( void ) const { return entries.size(); }
};

}

#endif
