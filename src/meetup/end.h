/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, an end: one end of a ticket, as it keeps itself registered with the meetup and has
 * datagrams passed through it (REQ15/PATH-4, REQ18/PATH-5; the proposal, revision 10). Path-finding
 * holds one, and knows no message of the meetup's: what it sends, and opens, is this. Logic: the
 * time and datagrams in, datagrams to send to the meetup out. No socket, no clock, no randomness.
 *
 * First where the meetup sees us (its STUN answer), then REGISTER with that, sealed; again until
 * it answers, and every REGISTER_EVERY after. The counter its messages are numbered by starts from
 * the time of day it was given, in ms, times 1024, so that a ticket used again by a new end is not
 * taken for an old one; and a REGISTER's time is reckoned from that time of day until the meetup
 * says otherwise (TIME).
 *
 * Absent: no answer to REGISTER_TRIES REGISTERs: ask again where we are. Late: no REGISTERED for
 * REGISTER_EVERY + ANSWER_LATE: thought not registered, and ask again where we are. Wrong: what is
 * not one of the meetup's messages, for this ticket and end, sealed by the meetup: not taken.
 * Lying: a REGISTERED that saw us anywhere but where we said, or a TIME that answers no REGISTER
 * of ours: not taken.
 */

#ifndef GOSH_MEETUP_END_H
#define GOSH_MEETUP_END_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "src/crypto/crypto.h"
#include "src/meetup/imp/messages.h"
#include "src/meetup/ticket.h"
#include "src/stun/stun.h"

namespace Gosh::Meetup {

class End
{
public:
  static const uint64_t REGISTER_EVERY = 25000; /* ms: registered again this often while it lasts... */
  static const uint64_t REGISTER_RETRY = 2000;  /* ...and this often until it is answered... */
  static const int REGISTER_TRIES = 3;          /* ...up to this many times; then where are we, again? */
  static const uint64_t ANSWER_LATE = 3500;     /* ms a REGISTERED may be late before we think we have moved */

private:
  bool known;
  Network::Addr meetup;
  Ticket ticket;
  int end; /* 0: the initiator; 1: the other */
  std::unique_ptr<Crypto::Session> session;
  uint64_t counter;
  std::string believed; /* where we appear to be, to the meetup ("where"), from its STUN answer */
  bool is_registered;
  int register_tries; /* REGISTERs not answered since we last asked where we are */
  uint64_t registered_at, last_register, last_stun;
  /* (REQ18/PATH-5) The meetup's clock, as we reckon it: the time of day when the meetup was
   * taken up, `now` then (taken at the first tick after), and what the meetup's TIME has told us
   * to add. And the counter of our last REGISTER, which a TIME must answer to be believed. */
  uint64_t wall, since;
  bool since_known;
  int64_t offset;
  uint64_t last_register_counter;
  bool time_wanted;

  uint64_t meetup_time( uint64_t now ) const
  {
    return static_cast<uint64_t>( static_cast<int64_t>( wall + ( now - since ) ) + offset );
  }

  /* One of the meetup's messages, from its address: true, having dealt with it. */
  bool read( const char* data, size_t n, uint64_t now, std::string& passed )
  {
    Imp::MeetupHeader h;
    if ( !Imp::meetup_read_header( data, n, h ) ) {
      return false;
    }
    if ( h.kind == Imp::MEETUP_PASSED ) {
      passed.assign( h.sealed, h.sealed_len );
      return true;
    }
    std::optional<Imp::Unsealed<Strong::Meetup::Registered>> seen;
    /* (The meetup answers only from where we said we were; one that says otherwise is old, or lying.) */
    if ( h.kind == Imp::MEETUP_REGISTERED && h.ticket == ticket.id && h.end == end
         && Imp::Sealed<Strong::Meetup::Registered, Imp::MeetupSealer>( h ).unseal( Imp::MeetupSealer{ *session, true }, seen )
         && Imp::meetup_registered_at( *seen, believed ) ) {
      is_registered = true;
      registered_at = now;
      register_tries = 0;
    }
    std::optional<Imp::Unsealed<Strong::Meetup::Time>> told;
    uint64_t answering, its_time;
    if ( h.kind == Imp::MEETUP_TIME && h.ticket == ticket.id && h.end == end && time_wanted
         && Imp::Sealed<Strong::Meetup::Time, Imp::MeetupSealer>( h ).unseal( Imp::MeetupSealer{ *session, true }, told )
         && Imp::meetup_read_time( *told, answering, its_time ) && answering == last_register_counter ) {
      /* (REQ18/PATH-5) Our REGISTER's time was too far from the meetup's clock: take its word,
       * which answers our last REGISTER and so is not an old one, and register again now. */
      offset += static_cast<int64_t>( its_time - meetup_time( now ) );
      time_wanted = false;
      register_tries = 0;
      last_register = 0;
    }
    return true;
  }

public:
  explicit End( bool initiator )
    : known( false ), meetup(), ticket(), end( initiator ? 0 : 1 ), session(), counter( 0 ), believed(),
      is_registered( false ), register_tries( 0 ), registered_at( 0 ), last_register( 0 ), last_stun( 0 ), wall( 0 ),
      since( 0 ), since_known( false ), offset( 0 ), last_register_counter( 0 ), time_wanted( false )
  {}

  /*
   * Use the meetup at this address ("ip:port"), with this ticket; begun again if one was in use.
   * False, and nothing changed, if the address or ticket is not one. wall_ms: the time of day, in ms.
   */
  bool use( const std::string& ip_port, const Ticket& t, uint64_t wall_ms )
  {
    Network::Addr where;
    if ( !Gosh::Stun::parse_ip_port( ip_port, where ) || t.id.size() != TICKET_ID_LEN || t.key.size() != TICKET_KEY_LEN ) {
      return false;
    }
    session.reset( new Crypto::Session( raw_key( t.key ) ) );
    meetup = where;
    known = true;
    ticket = t;
    counter = wall_ms << 10;
    wall = wall_ms;
    since_known = false;
    offset = 0;
    time_wanted = false;
    believed.clear();
    is_registered = false;
    register_tries = 0;
    last_register = last_stun = 0;
    return true;
  }

  /* Whether a meetup is in use; and where it is (an address of no family until one is). */
  bool in_use( void ) const { return known; }
  const Network::Addr& at( void ) const { return meetup; }

  /* Whether the meetup has taken us from where we are: it passes datagrams for us only then. */
  bool registered( void ) const { return is_registered; }

  /* What to send to the meetup now, if anything: its STUN question, or a REGISTER. Empty if nothing. */
  std::string tick( uint64_t now )
  {
    if ( !known ) {
      return std::string();
    }
    if ( !since_known ) {
      since = now;
      since_known = true;
    }
    if ( is_registered && now - registered_at > REGISTER_EVERY + ANSWER_LATE ) {
      is_registered = false; /* not answered for a while: perhaps we have moved; ask again where we are */
      believed.clear();
    }
    if ( believed.empty() ) {
      if ( !last_stun || now - last_stun >= REGISTER_RETRY ) {
        last_stun = now;
        /* (any twelve bytes will do for its id: the answer is known by where it comes from) */
        return Gosh::Stun::stun_request( "gosh meetup?" );
      }
      return std::string();
    }
    const uint64_t every = is_registered ? REGISTER_EVERY : REGISTER_RETRY;
    if ( last_register && now - last_register < every ) {
      return std::string();
    }
    if ( !is_registered && register_tries >= REGISTER_TRIES ) {
      /* Not taken from where we said: our NAT has moved us since. Ask again where we are. */
      believed.clear();
      register_tries = 0;
      last_stun = 0;
      return std::string();
    }
    register_tries++;
    last_register = now;
    last_register_counter = counter;
    time_wanted = true;
    return Imp::meetup_seal( *session, Imp::MEETUP_REGISTER, ticket.id, end, false, counter++, Imp::meetup_register_text( believed, meetup_time( now ) ) );
  }

  /* A STUN answer, mapped, came from `from`: if from the meetup, and we do not know, it says where to register from. */
  void stun_answer( const Network::Addr& from, const Network::Addr& mapped )
  {
    if ( known && Gosh::Stun::same_ip_port( from, meetup ) && believed.empty() ) {
      believed = Imp::meetup_where_of( mapped );
      last_register = 0;
    }
  }

  /*
   * A datagram came from `from`: true, having dealt with it, if it is one of the meetup's
   * messages from the meetup; false otherwise, for the caller to read. passed: the datagram the
   * other end sent through the meetup, if it was a PASSED; empty otherwise.
   */
  bool heard( const Network::Addr& from, const char* data, size_t n, uint64_t now, std::string& passed )
  {
    passed.clear();
    return known && Gosh::Stun::same_ip_port( from, meetup ) && read( data, n, now, passed );
  }

  /* A datagram for the other end, sealed as a PASS for the meetup to send on; empty unless registered(). */
  std::string pass( const std::string& datagram )
  {
    if ( !is_registered ) {
      return std::string();
    }
    return Imp::meetup_seal( *session, Imp::MEETUP_PASS, ticket.id, end, false, counter++, datagram );
  }
};

}

#endif
