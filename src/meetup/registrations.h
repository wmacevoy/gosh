/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, registrations: what a meetup does with REGISTER and PASS (REQ15/PATH-5; the proposal,
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
 * Absent: the other end not registered, or not heard for two minutes: nothing is passed. Late: an
 * end that has not registered again for two minutes is dropped; a ticket past its time is refused
 * and dropped. Wrong: anything that is not a message at its exact length, or whose seal does not
 * open as its kind, direction and end: dropped, unanswered. Lying: a REGISTER from anywhere but
 * the address sealed in it, or a counter used already: dropped. More than MAX_TICKETS tickets: a
 * new one puts out the one that has gone longest without passing a datagram. Nothing is answered
 * to a ticket the meetup did not make, and nothing for a PASS costs a key computation.
 */

#ifndef GOSH_MEETUP_REGISTRATIONS_H
#define GOSH_MEETUP_REGISTRATIONS_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "src/crypto/crypto.h"
#include "src/meetup/messages.h"
#include "src/meetup/ticket.h"

namespace Gosh {

/* Which counters an end has used, of the last WINDOW: the highest, and a mark for each below it. */
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

  /* True, and the counter marked used, if it has not been used and is not too far behind. */
  bool take( uint64_t c )
  {
    if ( !any ) {
      any = true;
      top = c;
      set( c );
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
};

class Registrations
{
public:
  static const size_t MAX_TICKETS = 64;
  static const uint64_t END_IDLE = 120; /* seconds an end is kept without registering again */

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
    uint64_t sent;     /* the meetup's own counter, for REGISTERED */
    uint64_t passed;   /* when it last passed a datagram; when made, if never */
    Entry() : session(), ends(), sent( 0 ), passed( 0 ) {}
  };
  std::map<std::string, Entry> entries;

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

  /* Room for one more: the ticket that has gone longest without passing a datagram goes. */
  void make_room( void )
  {
    if ( entries.size() < MAX_TICKETS ) {
      return;
    }
    std::map<std::string, Entry>::iterator oldest = entries.begin();
    for ( std::map<std::string, Entry>::iterator i = entries.begin(); i != entries.end(); i++ ) {
      if ( i->second.passed < oldest->second.passed ) {
        oldest = i;
      }
    }
    entries.erase( oldest );
  }

public:
  Registrations() : entries() {}

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
      if ( !meetup_open( *entry.session, h, false, counter, datagram ) || datagram.empty() || !self.counters.take( counter ) ) {
        return nothing;
      }
      const End& other = entry.ends[1 - h.end];
      if ( !live( other, now ) ) {
        return nothing;
      }
      entry.passed = now;
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
    uint64_t counter;
    std::string believed;
    if ( !meetup_open( *session, h, false, counter, believed ) || believed != from ) {
      return nothing;
    }
    if ( found == entries.end() ) {
      make_room();
      found = entries.insert( std::make_pair( h.ticket, Entry() ) ).first;
      found->second.session = std::move( fresh );
      found->second.passed = now;
    }
    Entry& entry = found->second;
    End& self = entry.ends[h.end];
    if ( !self.counters.take( counter ) ) {
      return nothing;
    }
    self.where = from;
    self.registered = now;
    Send s;
    s.any = true;
    s.to = from;
    s.bytes = meetup_seal( *entry.session, MEETUP_REGISTERED, h.ticket, h.end, true, entry.sent++, from );
    return s;
  }

  size_t ticket_count( void ) const { return entries.size(); }
};

}

#endif
