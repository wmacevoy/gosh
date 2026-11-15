# Proposal: a meetup that knows who it is passing for

**A proposal, not the design in force. Revision 3, 2026-11-15.** Nothing here is built yet.

The owner's decisions it is written to:

- "pf7 was a step to here. let it go. go for a tailscale. (or talescale-like if that breaks
  some ownership)"
- "what's wrong with a signed key exchange?" (nothing: revision 2's objection was the agent's
  mistake, section 9)
- "meetup keeps a list of signature authorities, public keys are signed by an authority, and a
  blacklist. tweet it is --- it's rare. so version 1+ --- ok?"

Revision 1 was attacked on paper by a session set to break it (twelve findings); revision 2
took them. This revision changes where the keys come from, keeps what that review established
(section 9), and is to be attacked again before anything is built
([SE1/W4](../vendor/se1/guide/08-work.md#w4)).

## 1. What is wrong today

A meetup has one key, shared by everyone allowed to use it. To have a datagram passed on, an
end puts twelve bytes in front of it: four random bytes and a tag of those four bytes. They
travel in the clear, say nothing of who sent them, when, or for which pair, and the meetup
treats whoever last named a pair as one of its ends. So anyone who sees one datagram can use
the meetup for ever and can take an end's place (`src/meetup/README.md`; `passing-test.cc`,
`test_replay`, with the real cipher).

## 2. What is taken, and from where

From Tailscale's relays, as the agent remembers their published design (none of its code or
wire format is used):

1. nothing reusable crosses the wire in the clear;
2. an end proves it holds a private key, afresh; watching gives an onlooker nothing;
3. a datagram is passed to a key, not to whoever spoke last;
4. no secret is shared between users, or handed out by whoever runs the relay.

From the owner: who may use a meetup is decided by **authorities** that sign users' keys, and
a **blacklist**.

**Code carried in: TweetNaCl** (version 20140427; Bernstein, van Gastel, Janssen, Lange,
Schwabe, Smetsers; public domain), one C file of about 700 lines, as `vendor/tweetnacl/`, with
where it came from, its checksum, and a test against published vectors. It gives the three
things needed: a box (a message sealed from one key pair to another), a signature, and a hash.
It is source gosh carries, not a library a machine can lack, so `REQ1/SHAPE-3` stands. It is
slow beside tuned code; it is used when an end registers, never for a relayed datagram.

## 3. The keys

| Key | Kind | Who holds the private half | Who has the public half | Made by |
| --- | --- | --- | --- | --- |
| an **authority's** | signature | whoever admits users (an instructor) | every meetup that trusts it, in a list | `gosh-server meetup-authority` |
| a **user's** | signature | the user, on their own machine; it never leaves | the authority, which signs it; the signed key is the user's **pass** | `gosh meetup-key`; then the authority signs |
| a **meetup's** | box | the meetup | its users, with its address (it is not a secret) | `gosh-server meetup-key` |
| a **session end's** | box | one end of one session, in memory, for that session | the other end (over ssh), and the meetup | each relay, when it starts |
| a registration's key, `Kr` | mosh's cipher | one end and the meetup | — | agreed when the end registers |

How they chain: an authority signs a user's key (the pass). The user's client end signs each
of its session's two ends' keys (a **voucher**), its own and the server end's, which the
server end sends it over ssh. So the end running on the server never holds the user's private
key, only a voucher and the pass, both of which are public.

A meetup trusts a pass if an authority on its list signed it and the user's key is not on its
blacklist. A meetup with no authorities passes nothing for anyone, and only says where an
asker appears to be (STUN), as an unkeyed one does today.

## 4. The protocol

UDP to the meetup's one port. Six kinds of datagram, each with a mark of its own (8 bytes), a
version byte, and one exact length (but the two that carry a datagram), checked before
anything is opened.

### 4.1 Registering (two round trips)

```
end -> meetup   HELLO       mark | the end's key E (32) | nonce (24) | boxed E -> meetup: { kind, version }
meetup -> end   COOKIE      mark | nonce (24)                        | boxed meetup -> E: { kind, version, cookie (16) }
end -> meetup   REGISTER    mark | E (32) | nonce (24)               | boxed E -> meetup:
                                { kind, version, cookie, the other end's key P (32),
                                  voucher: the user's signature over ( "gosh end", E ),
                                  pass:    the user's key U (32), the authority's signature over ( "gosh user", U ) }
meetup -> end   REGISTERED  mark | nonce (24)                        | boxed meetup -> E:
                                { kind, version, id (8), Kr (16),
                                  where the end appears to be: family (1), address (16), port (2) }
```

- A box can be opened only by its two key holders, and proves who sealed it. So a HELLO or
  REGISTER that opens shows the sender holds E's private half; an answer that opens shows it
  came from the meetup, and no one else can read `Kr`.
- Nonces are 24 random bytes; the end's and the meetup's differ in one fixed bit.
- **The cookie** (from revision 2): a number the meetup counts upward and a tag, under a
  secret it makes afresh each start, over that number, the HELLO's source address and E. Good
  for ten seconds at that address. It shows the end receives at the address it sends from and
  that the REGISTER is fresh, with no clock; and the costly checks (two signatures) are done
  only for a REGISTER that carries a good one.
- A REGISTER is taken if the box opens, the cookie is good, the pass is signed by a listed
  authority, U is not blacklisted, and the voucher is U's signature over E. It makes a
  **registration**: E at that address, wanting P, with `id` and `Kr`.
- For an E already registered: a lower cookie number is ignored; the same number is answered
  again as before; a higher one from the same address keeps `id` and `Kr`; a higher one from
  another address replaces it (the end moved).
- Anything that fails gets **no answer**, so a stranger cannot tell a meetup is there. (A
  replayed HELLO draws a cookie its replayer cannot open.)

### 4.2 Passing a datagram on

```
end -> meetup   PASS     mark | id (8) | counter (8) | the datagram, sealed under Kr with that counter
meetup -> end   PASSED   mark | the datagram
```

- The meetup, in this order: finds `id`; checks the source address is that registration's;
  opens the seal (mosh's cipher: fast); then marks `counter` used (a window of 1,024). Then it
  sends PASSED to the registration whose key is the P this one asked for, **if that one asked
  for this one's E in turn**, at its registered address, if heard within two minutes.
- An end takes a PASSED only from the address of a meetup it is registered with.
- At most 2,032 bytes of datagram (the cipher's buffer); a session's are about 1,300.

### 4.3 Keeping a registration, and losing one

- An end registers again every 25 seconds, and at once when nothing has come through the
  meetup for 3.5 seconds. The answer says where it appears to be, so a NAT that has changed
  its port is seen and mended in one exchange.
- The meetup forgets a registration unused for two minutes. It holds at most 2,048, and at
  most 16 for one user's key; past either, a new one is refused.

## 5. What an attacker can do

| Attacker | Today | Proposed |
| --- | --- | --- |
| Sees traffic; holds no pass | Uses the meetup for ever; takes an end's place | Nothing. Every registration message is boxed to or from the meetup; a copy opens for nobody else and is no good from another address. A copied PASS: wrong address, or counter used |
| Sees traffic and can forge an end's address; no pass | As above | Can replay only what the meetup did not receive (lost after they saw it): a REGISTER the end really sent, or a PASS delivered late. Cannot put their own payload in anything |
| Another user, with a pass of their own | Takes an end's place, if they see the pair's name | Uses the meetup (allowed). Cannot open anyone else's messages, cannot register as another's end (no private key for it), cannot be passed another pair's datagrams (both ends must name each other). Fills the table only to their own 16 |
| A user whose key is blacklisted, or whose authority is taken off the list | — | Refused at their next REGISTER, and the meetup drops the registrations they hold when it reads the new list |
| Someone who steals a user's private key | — | Is that user, until the key is blacklisted. Cannot touch sessions already running: those ends have keys of their own |
| Someone who steals an authority's private key | — | Can make passes, until that authority is taken off the list. Cannot touch other users' sessions |
| The meetup, or someone who takes its private key | Sees which addresses talk and how much; can drop | The same; and sees which users. Can pose as that meetup. Learns no user's or authority's private key |
| Anyone, with huge or malformed datagrams | (the fork never opened what it was sent) | Refused by length before anything is opened; what fails to open is caught |

In every row, nobody reads or changes what the two ends say: that is sealed by the session.

## 6. Each input: absent, late, wrong, lying

- **Absent.** No COOKIE or REGISTERED: the end asks again with a new nonce every few seconds
  and goes on with its other paths. The other end not registered, or not naming this one:
  datagrams are dropped. No pass configured: the end does not try a keyed meetup.
- **Late.** A cookie older than ten seconds is refused; the end asks for another. An old
  REGISTER is ignored (4.1). An old PASS is refused once 1,024 behind.
- **Wrong.** Wrong length, mark, version or kind; a box or seal that does not open; an unknown
  `id`; a pass no listed authority signed; a blacklisted user; a voucher that is not that
  user's: dropped, unanswered. **Silence has many causes**, so the end's log says which request
  went unanswered and for how long.
- **Lying.** An end naming a P that does not name it back is passed nothing. A user can vouch
  for any key it likes: that is what a user is allowed to do.

## 7. What this does not close

- **A PASSED proves nothing to the end that gets it.** Anyone forging the meetup's address can
  send one; the session refuses what is in it.
- **The meetup is trusted** not to drop, and sees who talks to whom, how much, and as which
  user.
- **A pass does not expire.** It ends when the user's key is blacklisted or its authority is
  taken off the list. (An expiry date would bring the meetup's clock into it; not proposed.)
- **TweetNaCl is slow, and was written to be small and auditable, not fast.** It does a few
  operations when an end registers. Registrations are capped per second, counted only after a
  good cookie.
- **Cost.** Each relayed datagram is sealed once more by the sender and opened once by the
  meetup, with mosh's cipher. This path is the last resort before ssh.
- **IPv4.** The meetup listens on IPv4, as today; the answer's address field fits IPv6.

## 8. What changes in gosh

- `vendor/tweetnacl/`: the file, where it came from, its checksum, and its licence; a unit
  test against the published vectors for the box, the signature and the hash. It needs random
  bytes from the system, which gosh supplies from where mosh takes its own.
- `src/meetup/`: `proof.*` goes. New logic, each with a test of bytes and numbers and no
  socket: `keys.h` (authorities, passes, vouchers, the blacklist: made and checked),
  `cookie.h`, `register.h` (the four registration messages; exact lengths),
  `registrations.h` (the table; 4.1 and 4.3), `passing.h` (PASS opened; the counter window;
  both ends naming each other). `serve.cc` stays the edge, and reads the list of authorities
  and the blacklist from files, again when told to.
- Commands: `gosh-server meetup-authority` (an authority's keys), `gosh meetup-key` (a user's
  keys), `gosh-server meetup-sign` (the authority signs a user's key: a pass),
  `gosh-server meetup-key` (a meetup's keys). Where each key is kept is the command's to say
  (queue item 10); a user's private key is a file only that user can read.
- `src/pathfinding/`: the end's half, as logic, when that part is reshaped (the rest of item
  8): an end's key pair for the session, the two ends' keys exchanged over ssh, the client end
  signing both vouchers. **Also found by the review, in the fork today:** `punch.cc` hands any
  probe-marked datagram of up to 65,528 bytes to the cipher's `decrypt`, whose buffer is 2,048
  bytes behind an `assert`. Path-finding is off in gosh; it is fixed before it is switched on.
- The attack tests of 2026-11-15 stay, turned round: each thing the onlooker could do becomes
  a check that they cannot; and a test for each row of section 5.
- No private key, `Kr`, or the bytes of a registration is ever written to a log.
- A gosh meetup and a pf.7 relay ignore each other (marks of their own). The requirements
  already say gosh does not work with the fork's releases at the other end (*Non-goals*).

## 9. How this proposal got here

- **Revision 1** used one shared key and the clock. A session set to break it found twelve
  things: among them that someone with the shared key who saw one registration kept that pair
  for the whole session from anywhere; that an oversized datagram would abort the meetup
  inside the cipher; that a tag over a counter alone let a payload be swapped; and that the
  clock rule failed silently on machines with wrong clocks and reopened replays on restart.
- **Revision 2** took them all: a key per user derived from a master, a cookie in place of the
  clock, datagrams sealed, exact lengths, a mark per kind, rules for an existing registration
  and for registering again. It kept to a shared-key cipher because the agent read
  `REQ1/SHAPE-3` as ruling out public keys. **That was a mistake:** it rules out a library a
  machine may lack, not a few hundred lines of source carried here.
- **Revision 3** (this) replaces every shared secret with key pairs, at the owner's word, and
  keeps from revision 2 what its review established: the cookie, sealed datagrams, the order
  of checks, exact lengths, marks, the rules for an existing registration, when to register
  again, the caps, the address field, no secrets in logs.

## 10. What the requirements would say (for a v15)

- **REQ1/PATH-5**, reworded (in a v15, which would then be the version to cite it by): *A
  meetup passes nothing on, and answers nothing, for anyone who has not just shown that they
  hold a key vouched for by a user whose key an authority the meetup trusts has signed, and
  which the meetup has not blacklisted; what it passes for an end goes only to the end that
  end names, and only if that end names it back; and a datagram copied off the network is no
  use to whoever copied it.* *(judged in part: "no use" is argued in the design and tested for
  each case it lists.)*
- **Glossary:** *authority*, *pass*, *voucher*, *registration*, *blacklist*.
- **Scope decisions:** a second exception to the freeze, for this; TweetNaCl carried in
  `vendor/`, with its origin and licence; a pass does not expire.
- **Security:** the meetup learns which user is behind a session, which it did not before.

## 11. Questions for the owner

1. **No expiry on a pass**, only the blacklist and the list of authorities (section 7). Agreed?
2. **The words**: authority, pass (a user's key signed by an authority), voucher (a session
   end's key signed by its user). Agreed, or others?
3. **A meetup with authorities stops answering STUN**; one with none answers STUN as today and
   passes nothing. Agreed?
