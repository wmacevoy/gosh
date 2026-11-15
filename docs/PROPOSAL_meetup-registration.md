# Proposal: a meetup that knows who it is passing for

**A proposal, not the design in force. Revision 4, 2026-11-15.** Nothing here is built yet.

The owner's decisions it is written to:

- "pf7 was a step to here. let it go. go for a tailscale. (or talescale-like if that breaks
  some ownership)"
- "what's wrong with a signed key exchange?" (nothing: revision 2's objection was the agent's
  mistake, section 10)
- "meetup keeps a list of signature authorities, public keys are signed by an authority, and a
  blacklist. tweet it is --- it's rare. so version 1+ --- ok?"

It has been attacked on paper twice by sessions set to break it
([SE1/W4](../vendor/se1/guide/08-work.md#w4)): revision 1 (twelve findings) and revision 3
(eleven). Both verdicts were "build, with these changes"; this revision has them. Section 10
says what each round changed.

## 1. What is wrong today

A meetup has one key, shared by everyone allowed to use it. To have a datagram passed on, an
end puts twelve bytes in front of it: four random bytes and a tag of those four bytes. They
travel in the clear, say nothing of who sent them, when, or for which pair, and the meetup
treats whoever last named a pair as one of its ends. So anyone who sees one datagram can use
the meetup for ever and can take an end's place (`src/meetup/README.md`; `passing-test.cc`,
`test_replay`, with the real cipher).

## 2. What is taken, and from where

From Tailscale's relays, as the agent remembers their published design (none of its code or
wire format is used): nothing reusable crosses the wire in the clear; an end proves it holds a
private key, afresh; a datagram is passed to a key, not to whoever spoke last; no secret is
shared between users or handed out by whoever runs the relay. From WireGuard, likewise from
memory: a cheap check that a sender knows the relay's public key, before any costly work.

From the owner: who may use a meetup is decided by **authorities** that sign users' keys, and
a **blacklist**.

**Code carried in: TweetNaCl** (version 20140427; Bernstein, van Gastel, Janssen, Lange,
Schwabe, Smetsers; public domain): one C file of about 700 lines, in `vendor/tweetnacl/`,
unedited, with where it came from and its checksum. It gives a box (a message sealed from one
key pair to another), a signature and a hash. It is source gosh carries, not a library a
machine can lack, so `REQ1/SHAPE-3` stands. It is used when an end registers, never for a
relayed datagram.

## 3. The keys

| Key | Kind | Who holds the private half | Who has the public half | Made by |
| --- | --- | --- | --- | --- |
| an **authority's** | signature | whoever admits users (an instructor) | every meetup that trusts it, in a list | `gosh-server meetup-authority` |
| a **user's**, U | signature | the user, in a file only they can read; it never leaves their machine | the authority, which signs it: that signature is the user's **pass** | `gosh meetup-key`; then the authority signs |
| a **meetup's**, M | box | the meetup | its users, with its address; it is not a secret | `gosh-server meetup-key` |
| a **session end's**, E | box | one end of one session, in memory, for that session | the other end (over ssh), and the meetup | each relay, when it starts |
| a registration's key, `Kr` | mosh's cipher | one end and the meetup | — | the meetup, at random, when the end registers |

**The chain.** An authority signs `("gosh user v1", U)`: the pass. The user's client end
signs, for each of the session's two ends, `("gosh end v1", M, E, P)`: a **voucher** that says
"at meetup M, end E may be passed to and from end P". The server end sends its E to the client
end over ssh and gets its voucher and the pass back; it never holds the user's private key.

**A voucher is for one meetup and one pair.** It is useful only while the other end's private
key exists, which is one session: a lifetime with no clock in it. A host that keeps the
vouchers of old sessions holds nothing it can use.

A meetup takes a pass if an authority on its list signed it and U is not on its blacklist. A
meetup started with no authorities passes nothing for anyone and only says where an asker
appears to be (STUN), as an unkeyed one does today.

## 4. The protocol

UDP to the meetup's one port. Each kind of datagram has a mark of its own (8 bytes), a version,
and one exact length (but the two that carry a datagram). **Lengths are checked before
anything is opened**, the cipher's included: it guards its buffer with an `assert` that some
builds leave out.

### 4.1 Registering (two round trips, for an end the meetup does not know)

```
end -> meetup   HELLO       mark | E (32) | nonce (24) | gate (16)
meetup -> end   COOKIE      mark | the HELLO's nonce (24) | cookie (24)
end -> meetup   REGISTER    mark | E (32) | nonce (24) | cookie (24) | boxed E -> M:
                                { kind, version, the cookie again,
                                  P (32), U (32), the voucher (64), the pass (64) }
meetup -> end   REGISTERED  mark | nonce (24) | boxed M -> E:
                                { kind, version, the cookie again, id (8), Kr (16),
                                  where the end appears to be: family (1), address (16), port (2) }
        or      REFUSED     mark | nonce (24) | boxed M -> E: { kind, version, the cookie again, why (1) }
```

What the meetup does, cheapest first, and where it stops:

1. **HELLO.** `gate` is the first 16 bytes of the hash of a label, M's public key, E and the
   nonce. One hash. A sender that does not know M gets no answer and learns nothing. No state
   is kept.
2. **COOKIE**, in the clear: the meetup's own time since it started (never the time of day,
   never backward) and a tag, under a secret it makes afresh each start, over that time, the
   address the HELLO came from, and E. One hash. Good for ten seconds, at that address, for
   that E. It shows the end receives where it sends from, and makes the REGISTER fresh,
   with no clocks agreeing.
3. **REGISTER.** The cookie is outside the box, and is checked first: one hash. Then a limit on
   how many REGISTERs one address may have opened per second. Only then the box is opened (the
   first costly step), which shows the sender holds E's private half; then the two signatures;
   then the lists.
4. A REGISTER is taken if all that holds, `P` is not `E`, the pass is signed by a listed
   authority, U is not blacklisted, and the voucher is U's signature over this M, E and P. It
   makes a **registration**: E at that address, for P, belonging to U, with a random `id` and a
   new `Kr`. If E is registered already and this cookie is older than the one it was made with,
   it is ignored; if it is the same cookie, the same answer is sent again.
5. Whatever fails **after** a good cookie is answered REFUSED, with why: the pass is not one
   this meetup takes; the user is blacklisted; the meetup is full. (The asker has had an answer
   already; silence would hide nothing and leave nobody able to tell a blacklisted student from
   an outage.) Whatever fails before, gets no answer.

What the end does: it takes a COOKIE only if it carries the nonce of a HELLO it has
outstanding, and a REGISTERED or REFUSED only if it carries the cookie it sent; anything else
is ignored and it goes on waiting. Each side checks the direction bit of the nonce it receives.
A box whose shared secret comes out all zero is refused.

### 4.2 Keeping a registration (one round trip, no public-key work)

```
end -> meetup   KEEP    mark | id (8) | counter (8) | sealed under Kr: { kind, version }
meetup -> end   KEPT    mark | counter (8)          | sealed under Kr: { kind, version, where the end appears to be }
```

- The meetup looks up U and its authority in the lists again; if either has gone it drops the
  registration and says nothing. Otherwise the registration is good for two more minutes.
- An end holds a registration at each meetup it is given for as long as path-finding is
  running, direct path or no, so that the meetup is ready when it is needed. It sends KEEP
  every 25 seconds, give or take a few.
- **When it starts again from HELLO:** when a KEEP or a PASS it sent through the meetup has had
  nothing back for 3.5 seconds; when KEPT shows its address has changed; when REGISTERED never
  came. Then after 3.5 s, 7 s, 14 s, ... up to a minute, each varied a little, so that the
  ends of a meetup that restarted do not all arrive at once.

### 4.3 Passing a datagram on

```
end -> meetup   PASS     mark | id (8) | counter (8) | the datagram, sealed under Kr with that counter
meetup -> end   PASSED   mark | the datagram
```

- The meetup, in this order: finds `id`; checks the source address is that registration's;
  checks the length; opens the seal; then marks `counter` used (a window of 1,024). It sends
  PASSED to the registration of the P this one is for, **if that one is for this one's E**, at
  its address, if it has been heard within two minutes.
- Counters under one `Kr` only go up, the end's and the meetup's told apart by one bit; a
  registration that is replaced gets a new `Kr`.
- An end takes a PASSED only from the address of a meetup it is registered with.
- At most 2,032 bytes of datagram; a session's are about 1,300.

### 4.4 How many, and who is put out

The meetup holds at most 2,048 registrations (about 10 MB), and 16 for one user (eight
sessions). When a user's seventeenth arrives, **that user's registration that has gone longest
without passing a datagram to an end that is for it** is put out. So what a user is using stays,
and what somebody is merely holding in their name goes first. When the meetup is full, a new
user's end is refused, and told so.

## 5. What an attacker can do

| Attacker | Today | Proposed |
| --- | --- | --- |
| Knows nothing of the meetup | Gets no answer | Gets no answer |
| Knows M (it is not a secret); holds no pass | Uses the meetup for ever with one copied datagram; takes an end's place | Gets cookies. Can make the meetup do one hash per datagram, and, from an address it really holds, a limited number of box openings a second. Can see that some key E registers, every 25 s, from where (it cannot tell whose). Cannot register, pass, or take a place |
| Sees traffic and can forge addresses; no pass | As above | Can replay to the meetup only what it did not receive (a REGISTER the end really sent; a PASS, delivered late). Can send an end a false COOKIE or a stale REGISTERED: neither is taken (4.1), but by answering first with a false COOKIE each time it can keep an end from registering. That is a denial by someone on the path |
| Another user, with a pass of their own | Takes an end's place, if they see the pair's name | Uses the meetup. Cannot open another's messages, register as another's end, or be passed another pair's datagrams. Spends only their own 16 |
| A server host the user logged in to (it holds that session's server-end key, voucher and the pass) | Holds the shared key: is a user of the meetup for ever | For that session, is that session's server end, as it must be. Afterwards holds a voucher that names an end nobody is, at one meetup: it can keep that one registration alive, which is put out first when the user needs room (4.4). Cannot relay for itself, at this or any other meetup |
| A user who is blacklisted, or whose authority is taken off the list | — | Told so at their next REGISTER; registrations they hold end at the next KEEP |
| Someone who steals a user's private key | — | Is that user until the key is blacklisted: can use the meetup, and can crowd the user's 16 so that a session of the user's whose registration lapses (a laptop asleep two minutes) may find no room. Cannot read or join a running session |
| Someone who steals an authority's private key | — | Can make passes until it is taken off the list; with many made-up users can fill the meetup. Taking the authority off also puts out its honest users |
| Someone who takes the meetup's private key | Sees which addresses talk and how much; can drop | **More than the meetup itself could do before:** can open every registration message they recorded (which user, at which address, for which peer, and each `Kr`), can pose as the meetup, and can register in place of any end they recorded while its pair lives. Not the content of any session. The remedy is a new key for the meetup |
| Anyone, with huge or malformed datagrams | (the fork never opened what it was sent) | Refused by length before anything is opened; what fails to open is caught |

In every row, nobody reads or changes what the two ends say: that is sealed by the session.

## 6. Each input: absent, late, wrong, lying

- **Absent.** No COOKIE: the meetup is down, unreachable, or the user has the wrong key for it;
  the end's log says "no answer to hello". No REGISTERED after a COOKIE: the same, said so.
  The end goes on with its other paths, and asks again as 4.2 says. The other end not
  registered, or not for this one: datagrams are dropped. No pass configured: the end does not
  try a meetup that needs one.
- **Late.** A cookie older than ten seconds is refused unanswered; the end asks for another.
  An old PASS is refused once 1,024 behind.
- **Wrong.** Before a good cookie: wrong length, mark, version, gate, or a cookie that is not
  good: dropped, unanswered. After one: REFUSED with why, or (a box that does not open, a
  voucher that is not that user's for this meetup and pair) dropped.
- **Lying.** An end for a P that is not for it is passed nothing. A user can vouch for any
  pair of keys: that is what a user is allowed to do, and it spends their own 16.
- **The lists** (authorities, blacklist) are files, read at start and again on a signal. A
  missing or unreadable file at start is a refusal to start. One that cannot be read later
  leaves the old list in force and says so; whoever edits one writes a new file and renames
  it. An **empty** list of authorities is not "no authorities": the meetup passes for nobody
  and does not turn into an open STUN answerer.

## 7. What this does not close

- **Whoever holds the meetup's private key** can read who registered and act as an end they
  recorded (5). There is no forward secrecy for that. A one-time key pair on each side of the
  handshake would give it, at one more costly step a side; not proposed. The remedy is to give
  the meetup a new key.
- **The meetup is trusted** not to drop, and sees who talks to whom, how much, and as which
  user. It writes none of that to its log (deferred by the owner, section 11), so whoever runs
  it cannot tell from the meetup alone whom to blacklist.
- **A PASSED proves nothing to the end that gets it.** Anyone forging the meetup's address can
  send one; the session refuses what is in it.
- **An end's key E is in the clear** when it registers, so one session can be followed across
  addresses and meetups by it. mosh's own datagrams already carry a counter in the clear.
- **Anyone who knows M can spend the meetup's time:** a hash per datagram, and a capped
  number of box openings from each real address. Passing shares that one thread.
- **A NAT that changes an end's port** costs 3.5 seconds and two round trips to mend, where a
  session of mosh's own follows a client at once.
- **A pass does not expire, and a blacklist is each meetup's own.** An authority cannot call a
  pass back; it tells the meetups. The authority must keep a record of whose key it signed, or
  nobody can write a blacklist: `meetup-sign` writes one.
- **A user's private key is a file** with no passphrase: any program running as that user can
  vouch. **Nothing tells a user they were given the wrong key for a meetup** but silence.
- **TweetNaCl** is small and slow; its signature check accepts some signatures in more than
  one form, and it does not refuse a few degenerate keys. Neither matters here as long as
  nothing is remembered by a signature's bytes (the fork remembered proofs so; this does not).
- **Cost.** Each relayed datagram is sealed once more by the sender and opened once by the
  meetup, with mosh's cipher. This path is the last resort before ssh.
- **IPv4.** The meetup listens on IPv4, as today; the answer's address field fits IPv6.

## 8. What changes in gosh

- `vendor/tweetnacl/`: the file as published, where it came from, its checksum and licence,
  compiled as C with flags of its own (it is not edited to quiet warnings). A wrapper,
  `src/meetup/nacl.h`, is the only thing that calls it: fixed sizes, its padding conventions
  hidden, an all-zero shared secret refused. Random bytes come from the system; if they cannot
  be had the program stops. Its unit test is the published vectors for the box, the signature
  and the hash, and is run once under a checker for undefined behaviour.
- `src/meetup/`: `proof.*` goes. New logic, each with a test of bytes and numbers and no
  socket: `keys.h` (passes and vouchers made and checked; the lists), `cookie.h`,
  `messages.h` (every kind made and opened; exact lengths), `registrations.h` (the table;
  4.1, 4.2, 4.4), `passing.h` (PASS opened; the counter window; both ends for each other).
  `serve.cc` stays the edge: the socket, the meetup's own time, the list files, the log.
- Commands: `gosh-server meetup-authority`, `gosh meetup-key`, `gosh-server meetup-sign`
  (which also writes the authority's record), `gosh-server meetup-key`. A user's private key
  is created at mode 0600, never overwritten, and not used if anyone else can read it.
- Between the two relays, over ssh: after its greeting the server end sends its E; the client
  end answers with the meetup's address and key, the voucher and the pass. Words an old relay
  does not know it already ignores. A server end that starts again has a new E and is vouched
  for again.
- `src/pathfinding/`: the end's half, as logic, when that part is reshaped (the rest of item
  8). **Found by the first review, in the fork today:** `punch.cc` hands any probe-marked
  datagram of up to 65,528 bytes to the cipher's `decrypt`, whose 2,048-byte buffer is guarded
  only by an `assert`. Path-finding is off in gosh; it is fixed before it is switched on.
- Tests: the attack tests of 2026-11-15 turned round (each thing the onlooker could do becomes
  a check that they cannot), and one for each row of section 5 that a test can reach.
- No private key, `Kr`, or the bytes of a registration is ever written to a log.
- A gosh meetup and a pf.7 relay ignore each other. The requirements already say gosh does not
  work with the fork's releases at the other end (*Non-goals*).

## 9. What the requirements would say (for a v15)

- **REQ15/PATH-5**, as v15 now words it (this was the proposal for it): *A
  meetup passes nothing on for anyone who has not just shown that they hold a key which a user
  has vouched for at that meetup, where an authority the meetup trusts has signed that user's
  key and the meetup has not blacklisted it; what it passes for an end goes only to the end
  that voucher names, and only if that end's names it back; a datagram copied off the network
  is no use to whoever copied it; and to someone who does not know the meetup's key it says
  nothing at all.* *(judged in part: "no use" is argued in the design and tested for each case
  it lists.)*
- **Glossary:** *authority*, *pass*, *voucher*, *registration*, *blacklist*.
- **Scope decisions:** a second exception to the freeze, for this; TweetNaCl carried in
  `vendor/` with its origin and licence; a pass does not expire.
- **Security:** the meetup learns which user is behind a session. It does not log it.

## 10. How this proposal got here

- **Revision 1**: one shared key and the clock. Attacked: twelve findings, among them that
  someone with the shared key who saw one registration kept that pair for the whole session
  from anywhere; that an oversized datagram would abort the meetup inside the cipher; that a
  tag over a counter alone let a payload be swapped; that the clock rule failed silently and
  reopened replays on restart.
- **Revision 2** took them: a key per user derived from a master, a cookie for the clock,
  sealed datagrams, exact lengths, a mark per kind. It kept to a shared-key cipher because the
  agent read `REQ1/SHAPE-3` as ruling out public keys. **That was a mistake:** it rules out a
  library a machine may lack, not source carried here.
- **Revision 3**: key pairs throughout, authorities and a blacklist, at the owner's word.
  Attacked: eleven findings. A voucher bound to nothing let a server host the user had once
  used hold their place for ever and lock them out of every meetup; every stranger's datagram
  cost a costly step that the cookie did not guard; answers were not tied to their requests;
  registering again every 25 seconds with signatures would not have fitted the meetup's one
  thread, and one rule would have fired for ever; silence for every failure left nothing
  diagnosable; and a leaked meetup key gave more than the table said.
- **Revision 4** (this) took those: the voucher names the meetup and the pair (3); the gate
  and the cookie come before any costly step (4.1); answers echo their requests (4.1); keeping
  a registration needs no public-key work, and when to start again is said (4.2); REFUSED says
  why (4.1); who is put out when a user's room runs out (4.4); the table of claims and the
  list of what is not closed corrected (5, 7); and the handling of the cipher, TweetNaCl and
  the list files said (4, 6, 8).

## 11. The owner's answers (2026-11-15)

1. **No expiry on a pass**, only each meetup's blacklist and list of authorities: "yes".
2. **The words** authority, pass, voucher: "yes".
3. **Whether a meetup with authorities still answers STUN**: "not sure --- there are lots of
   STUN servers --- but what's the harm?" The harm is small: it would tell a stranger that a
   meetup is there, which is the one thing the requirement says it does not tell someone who
   does not know its key; and it would be a public STUN server for anyone to lean on, at up to
   200 answers a second. The gain is nothing its own users need, since REGISTERED tells an end
   where it appears to be, and, as the owner says, there are many STUN servers. **So as
   proposed: it does not**, and turning it on later changes nothing else in this design.
4. **No forward secrecy for registrations**; the remedy for a leaked meetup key is a new key:
   "ok".
5. **The meetup's log naming each user's key and address**: "no - deferred". So the meetup
   writes no user's key or address to its log. What that leaves: whoever runs a meetup learns
   whom to blacklist only from the authority's own record of whose key it signed, not from the
   meetup.

## 12. Where a pass comes from: the owner's direction for revision 5 (2026-11-15, not yet written in)

Sections 3 to 9 above have an **authority** apart from the meetup that signs users' keys, by
hand. The owner, the same day, on how a user would actually get a signed key:

> "it seems like oauth to me ... i want to connect from a machine, i use oauth on the meetup
> server and it generates a (user_id,user_pub/user_sec) and passes
> meetup_pub,meetup_sign(user_id,user_pub). the meetup_pub could roll on 90 days, and only the
> last 4 are valid (re-auth 1/year for a new key), and a blacklist for recent keys? ... it
> leaves very little state the meetup service has to remember (a few signing keys created every
> 90 days, and a db of blacklist ids, kept for 1 year)."

and, to the agent's five suggested changes:

1. The user's machine makes its key pair, and sends only the public half: "agree (no reason
   to keep the private key, but no reason to make it there either)".
2. A separate authority that signs, apart from the meetup: **no.** "that's just more state, all
   we need to check later is if one of the meetup(time) keys (at most 4) sign the message, no
   other state." The meetup signs with its own keys.
3. Keeping only the newest key's private half: **no.** "you still need to check the older keys,
   so no, you need the last 4." (And since what an end later sends is sealed to the public half
   of the key it enrolled under, the meetup needs the private halves of all four to open it.)
4. An opaque user id: **not needed.** "the sent message (for the actual meetup later) is
   encrypted by the meetup(time) public key, so it's opaque already. no pki --- that's just
   another layer to figure out."
5. Handing out the meetup's address: **not needed.** "the meetup address is the service, no
   hand-out, you already found it. gosh-meetup is a service."

So, for revision 5: **one service, gosh-meetup.** A user signs in to it (OAuth) and it signs
`(user_id, user_pub)` with its current key; every 90 days it makes a new key, and the last four
are good, so a pass lasts nine months to a year and is renewed by signing in again; it keeps a
blacklist for a year. There are no authorities apart from the meetup, and no list of them. An
end's later messages are sealed to the public half of the key its pass was signed under. What
stays from sections 3 to 9: vouchers, registrations, the cookie and the gate, passing to a key,
keeping a registration, who is put out, and everything about lengths and the carried library.
This **replaces the owner's earlier "yes" to a pass that does not expire.**

And how a machine enrols (the owner, 2026-11-15): "the start is a local public/secret key where
the public key url-encoded to the mosh-meetup": the user's machine makes the key pair, and its
public half goes to the service in the address the user opens to sign in. The private half
never leaves the machine.

