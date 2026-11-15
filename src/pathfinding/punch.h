/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef PUNCH_HPP
#define PUNCH_HPP

#include <memory>
#include <string>
#include <vector>

#include "src/crypto/crypto.h"
#include "src/meetup/messages.h"
#include "src/meetup/ticket.h"
#include "src/network/network.h"

/*
 * A way straight through, for two machines that are each behind an
 * address translator (a NAT) and so cannot simply be sent to.
 *
 * A NAT lets a datagram in from outside only where one has lately gone
 * out the other way.  So if both ends send to each other at once, each
 * one's NAT takes the other's datagrams for answers to its own, and
 * lets them in: "hole punching" (RFC 5128).  For that each end has to
 * know where the other appears to be, from outside.  It finds out its
 * own outside address by asking a server that will say where a datagram
 * came from (STUN, RFC 8489); it adds its addresses on its own network,
 * in case the other end is on that too; and the two tell each other over some
 * way they already have of talking: here, the ssh connection that
 * carries the relay (relay.h), whose two ends hold one of these each.
 *
 * It does not always work.  Some NATs give a different outside address
 * for each destination, and then what the server saw is no use to
 * anyone else.  Nothing is lost by trying: the relay goes on carrying
 * the session over ssh until a way through is found, and again if it
 * closes.  (gosh) And where the user has a meetup, and a ticket for it
 * fetched over ssh (src/meetup/ticket.h), the two ends register with it
 * and their datagrams can go through it (REQ15/PATH-4, REQ15/PATH-5):
 * the second-best way, kept while a straight one is looked for.
 *
 * On the socket there are three kinds of datagram, told apart by how
 * they begin:
 *   probes   an 8-byte mark, then a sealed message: "are you there",
 *            "I am", "goodbye".  Sealed with a key the two ends share
 *            (sent over ssh), and numbered, so that only the other end
 *            can say where the other end is.  Exactly PROBE_LEN bytes:
 *            (gosh) anything else is not opened, since the cipher guards
 *            its buffers with an assert alone.
 *   STUN     answers from the server asked.
 *   meetup   (gosh) REGISTERED and PASSED from the meetup (messages.h).
 *   the rest the session's own datagrams, taken only from where the
 *            other end was last heard to be.  (They are mosh's, and
 *            sealed already.)
 *
 * IPv4 only: that is where the NATs are.
 */

namespace Network {

/* The mark that begins a probe, and a control message between relays (relay.h). */
extern const char PUNCH_MARK[8];
/* A STUN request for our outside address: txid is 12 bytes of anything. */
std::string stun_request( const char* txid );
/* If this is an answer to one that says where we were seen: true, and where. */
bool stun_parse( const char* data, size_t len, Addr& mapped );

/* "1.2.3.4:567" both ways.  IPv4 only. */
bool parse_ip_port( const std::string& text, Addr& addr );
std::string format_ip_port( const Addr& addr );

class Punch
{
public:
  static const uint64_t FAST_FOR = 8000;    /* ms of probing every... */
  static const uint64_t FAST_EVERY = 250;   /* ...this often, then every... */
  static const uint64_t SLOW_EVERY = 2000;  /* ...this often, while there is no way through */
  static const uint64_t OPEN_EVERY = 1000;  /* and this often once there is, to know that there still is */
  static const uint64_t OPEN_FOR = 3500;    /* how long without hearing before it is thought closed */
  static const size_t MAX_CANDIDATES = 16;
  static const size_t PROBE_LEN = 8 + 8 + 1 + 16; /* the mark; the nonce, one byte, the tag */
  static const uint64_t REGISTER_EVERY = 25000;   /* ms: registered again this often while it lasts... */
  static const uint64_t REGISTER_RETRY = 2000;    /* ...and this often until it is answered */

  enum Got
  {
    NOTHING, /* nothing there to read */
    OTHER,   /* something of ours, or of nobody's: dealt with */
    DATA,    /* a datagram of the session's */
    GOODBYE  /* the other end has finished */
  };

private:
  int sock;
  bool initiator;
  std::unique_ptr<Crypto::Session> session;
  uint64_t send_seq, heard_seq;
  std::vector<Addr> candidates;       /* where the other end might be */
  std::vector<std::string> local_new; /* where we appear to be, not yet told to the other end */
  std::vector<std::string> local_all;
  Addr peer;     /* where it was last heard to be... */
  bool peer_via; /* ...or the server that passes datagrams on to it, if that is how it was heard */
  bool locked;
  uint64_t heard, started, last_probe, last_direct_probe;

  /* (gosh) the meetup, once there is a ticket for it: where it is, and where it saw us */
  bool meetup_known;
  Addr meetup;
  Gosh::Ticket ticket;
  int end; /* 0: the initiator; 1: the other */
  std::unique_ptr<Crypto::Session> meetup_session;
  uint64_t meetup_counter;
  std::string believed; /* where we appear to be, to the meetup ("where"), from its STUN answer */
  bool registered;
  uint64_t registered_at, last_register, last_meetup_stun;

  void transmit( const std::string& packet, const Addr& to, bool via );
  void probe( char type, const Addr& to, bool via );
  void tend_meetup( uint64_t now );
  bool from_meetup( const char* data, size_t n, uint64_t now, std::string& passed );

  /* unused */
  Punch( const Punch& );
  Punch& operator=( const Punch& );

public:
  /*
   * key: as Crypto::Base64Key prints it.  The end that made it up is the
   * initiator.  ask: "host:port" of STUN servers, asked (from another
   * process, so as not to wait on name lookups) where we appear to be.
   * Throws NetworkException if there is no socket to be had.
   */
  Punch( bool s_initiator, const std::string& key, const std::vector<std::string>& ask, uint64_t now );

  /*
   * (gosh) Use a meetup as well: at this address ("ip:port"), with this ticket. False if the
   * address is not one. The counter its messages are numbered by starts from the time of day,
   * so that a ticket used again by a new Punch is not taken for an old one.
   */
  bool use_meetup( const std::string& ip_port, const Gosh::Ticket& t );
  bool meetup_registered( void ) const { return registered; }
  ~Punch();

  int fd( void ) const { return sock; }
  uint16_t local_port( void ) const;

  void add_candidate( const std::string& ip_port );
  /* Where we may be reached, as far as has been learnt since this was last asked. */
  std::vector<std::string> take_local_candidates( void );

  void tick( uint64_t now );
  /* Reads one datagram, if there is one. */
  Got receive( uint64_t now, std::string& datagram );

  /* Whether there is, as far as we know, a way through just now. */
  bool open( uint64_t now ) const { return locked && now - heard < OPEN_FOR; }
  uint64_t last_heard( void ) const { return locked ? heard : 0; }
  /* Where the other end is; or which meetup, if datagrams are being passed on to it through one. */
  std::string where( void ) const
  {
    return locked ? ( peer_via ? "the meetup at " : "" ) + format_ip_port( peer ) : std::string();
  }
  bool direct( void ) const { return locked && !peer_via; }

  void send( const std::string& datagram );
  void goodbye( void );
};

}

#endif
