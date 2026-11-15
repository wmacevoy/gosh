/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#ifndef GOSH_MEETUP_H
#define GOSH_MEETUP_H

#include <string>

namespace Network {

/*
 * A meetup: a machine with a public address where the two ends of a
 * session, each behind its NAT, can find each other.  ("mosh-server
 * meetup PORT")  It does two things.
 *
 * It answers the STUN question: it says where a request came from.
 * With a key it answers only requests that show the key; and in any
 * case no more than a few hundred a second, so as to be no use to
 * anyone for anything else.
 *
 * And with a key, for two ends that can find no way to each other, it
 * passes their datagrams between them, for as long as they need.  Each
 * sends it
 *     MEETUP_MARK, twelve bytes showing the key, sixteen naming the pair, the datagram
 * and it sends the same on to whichever other address has lately sent
 * it datagrams naming that pair.  So the two still talk by UDP, though
 * through here.  It reads nothing of what it passes (which is sealed by
 * its ends), and passes nothing for anyone without the key.
 *
 * Runs until killed; returns an exit status if it cannot start.
 */
int meetup_serve( const char* port, const std::string& key );

}

#endif
