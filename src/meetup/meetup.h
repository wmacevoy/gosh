/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef GOSH_MEETUP_H
#define GOSH_MEETUP_H

#include <string>

namespace Network {

/*
 * A meetup: a machine with a public address, typically the student's own server, where the two
 * ends of a session that cannot reach each other find where they appear to be, and through which
 * their datagrams are passed when they can find no way to each other ("gosh-server meetup PORT";
 * REQ15/PATH-4, REQ18/PATH-5). It does two things.
 *
 * It answers the STUN question, to anyone: it says where a request came from, never with more
 * bytes than it was sent, and no more than a few hundred times a second (answer.h).
 *
 * And for the two ends of a ticket it made (ticket.h), each registered from where it is, it passes
 * their datagrams between them (registrations.h). It reads nothing of what it passes, which is
 * sealed by the session, and passes nothing for anyone without a ticket.
 *
 * master: its master key (master.h). Runs until killed; returns an exit status if it cannot start.
 */
int meetup_serve( const char* port, const std::string& master );

}

#endif
