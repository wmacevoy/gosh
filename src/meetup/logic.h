/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * meetup, its logic: what a meetup does with each datagram it hears (meetup.h says what that is),
 * with no socket and no clock (REQ15/PATH-4, REQ18/PATH-5). serve.cc is the edge that runs it on
 * sockets; a test of another part runs it with plain numbers, and knows no message of the meetup's.
 */

#ifndef GOSH_MEETUP_LOGIC_H
#define GOSH_MEETUP_LOGIC_H

#include <cstdint>
#include <exception>
#include <string>

#include "src/meetup/imp/answer.h"
#include "src/meetup/imp/messages.h"
#include "src/meetup/imp/registrations.h"
#include "src/meetup/ticket.h"
#include "src/stun/stun.h"

namespace Gosh::Meetup {

class Logic
{
public:
  /*
   * What to send for a datagram heard. any: whether it was taken (a STUN question answered, or one
   * of an end's messages taken from it); if not, nothing is sent. to, to_len: where to send `bytes`,
   * to_len 0 if nowhere. stun: whether it is a STUN answer, to the asker; if not, it is to an end,
   * whose "where" is to_where, and passed says whether it is a datagram the other end passed (a
   * PASSED; if not, one of the meetup's own answers).
   */
  struct Send
  {
    bool any;
    bool stun;
    bool passed;
    Network::Addr to;
    socklen_t to_len;
    std::string to_where;
    std::string bytes;
    Send() : any( false ), stun( false ), passed( false ), to(), to_len( 0 ), to_where(), bytes() {}
  };

private:
  /* A ticket's key, under the master key; anything that goes wrong in working it out is no key. */
  struct KeyOf
  {
    const std::string& master;
    std::string operator()( const std::string& id ) const
    {
      try {
        return ticket_key( master, id );
      } catch ( const std::exception& ) {
        return std::string();
      }
    }
  };

  std::string master;
  Imp::Registrations registrations;
  Imp::AnswerRate rate;

public:
  /* started: when the meetup started, UTC seconds. master_key: its master key (master.h). */
  Logic( uint64_t started, const std::string& master_key ) : master( master_key ), registrations( started ), rate() {}

  /* A datagram, data and n, heard from `from` at `now` (UTC seconds). From an address of no family it can say: nothing. */
  Send heard( const char* data, size_t n, const Network::Addr& from, uint64_t now )
  {
    Send out;
    const std::string where = Imp::meetup_where_of( from );
    if ( where.empty() ) {
      return out;
    }
    if ( Gosh::Stun::is_stun_request( data, n ) ) {
      if ( rate.allow( now ) ) {
        out.any = true;
        out.stun = true;
        out.to_where = where;
        out.bytes = Imp::stun_answer_to( data, where );
        if ( !Imp::meetup_addr_of( where, out.to, out.to_len ) ) {
          out.to_len = 0;
        }
      }
      return out;
    }
    const Imp::Registrations::Send s = registrations.datagram( data, n, where, now, KeyOf{ master } );
    if ( s.any ) {
      out.any = true;
      out.to_where = s.to;
      out.bytes = s.bytes;
      Imp::MeetupHeader h;
      out.passed = Imp::meetup_read_header( s.bytes.data(), s.bytes.size(), h ) && h.kind == Imp::MEETUP_PASSED;
      if ( !Imp::meetup_addr_of( s.to, out.to, out.to_len ) ) {
        out.to_len = 0;
      }
    }
    return out;
  }

  /* What a meetup sends an end for a datagram the other end passed it (a PASSED): for a test that plays the meetup's part in one step. */
  static std::string passed( const std::string& datagram ) { return Imp::meetup_passed( datagram ); }
};

}

#endif
