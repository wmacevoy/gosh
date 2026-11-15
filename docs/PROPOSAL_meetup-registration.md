# Proposal: a meetup that knows who it is passing for

**A proposal, not the design in force. Revision 2, 2026-11-15.** Drafted by the agent at the
owner's word: "pf7 was a step to here. let it go. go for a tailscale. (or talescale-like if
that breaks some ownership)". It replaces how a meetup decides whom to pass datagrams for
(`src/meetup/`, queue item 8). Nothing here is built yet.

Revision 1 was given to a session whose job was to break it on paper
([SE1/W4](../vendor/se1/guide/08-work.md#w4): a wire format with keys in it is a one-way door
once released). Its verdict: build, with changes. Twelve findings; three cells of revision 1's
table of claims were wrong, and its "what a shared-key cipher cannot give" was wrong. This
revision takes them all. Section 9 lists what changed and why.

## 1. What is wrong today

A meetup has one key, shared by everyone allowed to use it. To have a datagram passed on, an
end puts twelve bytes in front of it: four random bytes and a tag of those four bytes. They
travel in the clear, say nothing of who sent them, when, or for which pair, and the meetup
treats whoever last named a pair as one of its ends. So anyone who sees one datagram can use
the meetup for ever and can take an end's place (`src/meetup/README.md`; `passing-test.cc`,
`test_replay`, with the real cipher).

## 2. What is taken from Tailscale's relays

From the agent's memory of Tailscale's published design, not checked against its source;
**none of its code or wire format is used**, so no question of whose it is arises.

1. Nothing reusable crosses the wire in the clear.
2. Each end proves it holds a key, afresh; seeing the exchange gives an onlooker nothing.
3. The relay passes a datagram to an identity, not to whoever spoke last.
4. *(added in revision 2)* Each user has a key of their own, so one user cannot act as another.

Tailscale gets these from TLS and a public key for every node. gosh uses only the cipher mosh
already carries (AES-OCB: a shared-key cipher that seals and proves in one pass), because
`SHAPE-3` allows no dependency a bare machine lacks. All four are had with that cipher.

## 3. The keys

| Key | Who holds it | Made by | Used for |
| --- | --- | --- | --- |
| the meetup's **master** | the meetup alone | `gosh-server meetup-master` | making and checking users' keys; never leaves the meetup's machine |
| a **user's key**: a number (8 bytes) and `Ku` (16 bytes) | one user, and the meetup can work it out | `gosh-server meetup-key`, run by whoever holds the master | sealing that user's registrations |
| the pair's name at this meetup, `Vm` | the two ends of one session, and the meetup once they register | each end, from the secret the two relays already share | says which two registrations are a pair; never in the clear |
| a registration's key, `Kr` | one end and the meetup | the meetup, at random | sealing each datagram to be passed |

- `Ku` is the tag of sealing nothing under the master with the user's number as the nonce.
  The meetup stores no list of users: it recomputes `Ku` from the number, which travels in the
  clear. Only someone given `(number, Ku)` by the holder of the master can seal with it. A
  number can be refused (a list of numbers no longer welcome).
- **One user cannot open another's registration.** Both ends of a session are one user's and
  have the same key, as they have the same meetup key today. Whoever runs the meetup chooses:
  a key for each student, or one for the class (and then the class shares the risk in 6).
- `Vm` is the tag of sealing the meetup's name, as the user wrote it, under the pair's probe
  key. It differs at every meetup, so one meetup cannot use it at another.
- The user's key is 24 bytes: 32 letters where today's is 22.

## 4. The protocol

UDP to the meetup's one port. Five kinds of datagram, each beginning with a mark of its own
(8 bytes), so that none is taken for another, or for anything of the fork's. Every sealed part
begins with its kind and the protocol's version, so neither can be changed in the clear. Each
kind has one exact length (but PASS and PASSED, which carry a datagram), checked before
anything is opened.

### 4.1 Registering (two round trips)

```
end -> meetup   HELLO       mark | number(8) | nonce(8) | sealed under Ku: { kind, version }
meetup -> end   COOKIE      mark | nonce'(8)            | sealed under Ku: { kind, version, cookie(16) }
end -> meetup   REGISTER    mark | number(8) | nonce(8) | sealed under Ku: { kind, version, cookie(16), role(1), Vm(16) }
meetup -> end   REGISTERED  mark | nonce'(8)            | sealed under Ku: { kind, version, id(8), Kr(16),
                                                           where the end appears to be: family(1), address(16), port(2) }
```

- A `nonce` is 8 bytes the end draws at random, with its top bit clear. `nonce'` is the same
  with the top bit set: the meetup never chooses a nonce under `Ku`, so restarting it cannot
  repeat one. Each side checks the bit on what it receives. An answer that opens under
  `nonce'` is the answer to that request and no other; the end takes answers only to requests
  it still has outstanding.
- **The cookie replaces any clock.** It is a number the meetup counts upward (never the time
  of day, never backward) and a tag, under a secret the meetup makes afresh each time it
  starts, over that number, the address the HELLO came from, and the user's number. It is good
  for ten seconds, at that address only. It shows the end can receive at the address it
  sends from, and that the REGISTER is fresh. No clocks need agree.
- A REGISTER whose cookie is good makes a **registration**: `(user, Vm, role)` at that address,
  with a new `id` and `Kr`. If there is one already for the same `(user, Vm, role)`:
  - a cookie numbered lower than the one it was made with is ignored (an old REGISTER, late or
    replayed);
  - the same number gets the same answer again (the answer was lost);
  - a higher number from the same address keeps `id` and `Kr` (a refresh) and answers again;
  - a higher number from another address replaces it (the end moved): new `id`, new `Kr`.
- `role` is 0 for the end that made the pair's probe key and 1 for the other.
- A HELLO or REGISTER that is the wrong length, does not open, has the wrong kind, version or
  direction bit, or a cookie that is not good, gets **no answer**. A stranger cannot tell a
  meetup is there. (Someone who replays a HELLO they saw gets a cookie they cannot open: a
  fixed-size answer, to someone who already knew.)
- A meetup with a key answers no STUN question: REGISTERED says where the end appears to be.

### 4.2 Passing a datagram on

```
end -> meetup   PASS     mark | id(8) | counter(8) | the datagram, sealed under Kr with that counter
meetup -> end   PASSED   mark | the datagram
```

- The meetup, in this order: finds the registration `id`; checks the datagram came from its
  address; opens the seal; and only then marks `counter` as used (a window of 1,024). Any
  failure: dropped, unanswered. Then it sends PASSED to the registration with the same
  `(user, Vm)` and the other role, at its registered address, if that one has been heard
  within two minutes.
- The datagram is sealed, not merely tagged: a tag over the counter alone would let someone
  who saw a PASS put their own payload behind it. At most 2,032 bytes (the cipher's buffer);
  a session's datagrams are about 1,300.
- An end takes a PASSED only from the address of a meetup it holds a registration with.

### 4.3 Keeping a registration, and losing one

- An end registers again every 25 seconds, and at once when it has heard nothing through the
  meetup for 3.5 seconds (path-finding's own measure of a closed path). The answer says where
  it appears to be, so a NAT that has changed its port is seen and mended in one exchange.
- The meetup forgets a registration not used for two minutes. It holds at most 2,048 (about
  9 MB), and at most 16 for one user's number; past either, a new one is refused.

## 5. What an attacker can do

| Attacker | Today | Proposed |
| --- | --- | --- |
| Sees traffic; no key | Uses the meetup for ever; takes an end's place | Nothing. A copied HELLO earns a cookie they cannot open. A copied REGISTER is no good from their address, and from the true address is the same request again or an old one. A copied PASS is refused: wrong address, or counter used |
| Sees traffic and can forge the true end's address; no key | As above | Can replay a REGISTER or a PASS that the true end sent and the meetup **did not receive** (lost on the way after they saw it): the first changes nothing the end did not ask for; the second delivers the true datagram late. Cannot put their own payload in a PASS |
| Another user of the same meetup, with a key of their own | Takes an end's place, if they see the pair's name | Uses the meetup (allowed). Cannot open another user's HELLO, REGISTER or PASS, sees no `Vm`, cannot register as another user's end. Can fill the meetup's table only up to their own 16 |
| Someone given the **same** user key (a class sharing one), who sees a pair's traffic | Takes an end's place | Opens the REGISTER, learns `Vm`, and can register as that end, from anywhere, for as long as the session lives. A denial; they see sealed datagrams. Not closed: this is what a key each is for |
| The meetup itself, or someone who takes its master | Sees which addresses talk and how much; can drop | The same; can also make any user's key. `Vm` is no use at another meetup |
| Anyone, sending the meetup huge or malformed datagrams | (the fork never opened what it was sent) | Refused by length before anything is opened; what fails to open is caught |

In every row, nobody reads or changes what the two ends say: that is sealed by the session.

## 6. Each input: absent, late, wrong, lying

- **Absent.** No COOKIE or REGISTERED comes: the end asks again, with a new nonce, every few
  seconds, and goes on with its other paths; a meetup is one way among several. The other end
  has not registered: datagrams are dropped.
- **Late.** A cookie older than ten seconds is refused; the end asks for another. An old
  REGISTER is ignored (4.1). An old PASS is refused once its counter is 1,024 behind.
- **Wrong.** Wrong length, mark, kind, version, direction bit, a seal that does not open, an
  unknown `id`, a number on the unwelcome list: dropped, unanswered. **Silence has several
  causes** (wrong key, old version, full table, no meetup): the end's log says which request
  went unanswered and for how long, since the meetup will not say why.
- **Lying.** A user's two ends both claiming one role put each other out; only one's own ends
  can do that. A false `Vm` pairs with nobody.

## 7. What this does not close

- **People sharing one user key** can put each other out (5, fourth row). The remedy is a key
  each, which whoever runs the meetup can give.
- **A PASSED proves nothing to the end that gets it.** Anyone who forges the meetup's address
  can send one; the session refuses what is in it. Sealing PASSED for the receiving end would
  close this at the cost of a third pass of the cipher over every relayed datagram; not
  proposed.
- **The meetup is trusted** not to drop, and sees who talks to whom and how much.
- **Cost.** Each relayed datagram is sealed once more by the sending end and opened once by
  the meetup. A meetup's path is the last resort after a direct path and before ssh; its speed
  is not what `SPEED-1` measures.
- **IPv4.** The meetup listens on IPv4 only, as today. The answer's address field is long
  enough for IPv6, so that carrying it later changes no format.

Public keys (a pair for every end, as Tailscale has) would close the first of these without
needing a key per user. They need a primitive that one of the cipher libraries mosh can be
built with does not have. With a key per user available, that is not proposed.

## 8. What changes in gosh

- `src/meetup/`: `proof.*` goes. New logic, each with a test of numbers and bytes and no
  socket: `userkey.h` (a user's key from the master; the unwelcome list), `cookie.h`,
  `register.h` (the four registration messages, made and opened; exact lengths),
  `registrations.h` (the table and its rules, 4.1 and 4.3), `passing.h` (PASS opened, the
  counter window, where to send). `serve.cc` stays the edge. `gosh-server meetup-master`
  makes a master; `gosh-server meetup-key` makes a user's key from the master in the
  environment.
- `src/pathfinding/`: the end's half of the same, as logic, when that part is reshaped (the
  rest of item 8). **Found by the review and in the fork today:** `punch.cc` hands any
  probe-marked datagram up to 65,528 bytes to the cipher's `decrypt`, whose buffer is 2,048
  bytes guarded by an `assert`. Path-finding is off in gosh, so it cannot be reached now; it
  is fixed before it is switched on.
- The attack tests of 2026-11-15 stay, turned round: each thing the onlooker could do becomes
  a check that they cannot. A test for each row of 5.
- Secrets (`Vm`, `Kr`, a user's key, the master, the bytes of a registration) are never
  written to a log.
- A gosh meetup and a pf.7 relay do not understand each other, and with marks of their own
  they ignore each other, not misread each other. The requirements already say gosh does not
  work with the fork's releases at the other end (*Non-goals*, since v6).

## 9. What revision 2 changed, from the review

| Finding | Change |
| --- | --- |
| A key-holder who saw one registration kept the pair for the whole session, from anywhere; and a shared-key cipher *can* prevent it | A key for each user, derived from a master (3). The choice put to the owner in revision 1, shared key or public keys, was a false one |
| An oversized datagram would abort the meetup in the cipher's `decrypt` | Exact lengths checked before opening; failures caught (4, 5 last row) |
| A key-holder could fill the table and shut everyone out, or have the meetup send to a victim | Caps per user; and the cookie proves an end receives at its address (4.1, 4.3) |
| A copied REGISTER was good again after a restart, a clock step, at a second meetup, or by winning a race; and a late one could silently strand an end | The cookie in place of the clock; the rules for an existing registration (4.1); `Vm` per meetup |
| The clock rule failed silently on machines with bad clocks | No clock (4.1). Revision 1's question 2 is gone |
| A PASS's tag did not cover its datagram | The datagram is sealed under `Kr` (4.2); the order of checks stated |
| The kinds could not be told apart; nothing bound kind or version | A mark each; kind and version inside every seal (4) |
| The meetup's nonces would repeat after a restart | The meetup uses the end's nonce with a bit set (4.1) |
| A NAT changing port left an end stranded with no way back | When an end registers again (4.3) |
| The pair's secret was of use at other meetups | `Vm` is per meetup (3) |
| A PASSED proved nothing | Taken only from a meetup the end is registered with; the rest said plainly (7) |
| IPv6, secrets in logs, memory | Address field sized for IPv6; no secrets logged; a cap of 2,048 (4.3, 7, 8) |

## 10. What the requirements would say (for a v15)

- **PATH-5**, reworded: *A meetup given a master answers nothing, and passes nothing on, for
  anyone who has not just shown it a user's key; what it passes for a pair goes only to that
  pair's two ends; and a datagram copied off the network is no use to whoever copied it.*
  *(judged in part: "no use" is argued in the design and tested for each case it lists.)*
- **Glossary:** *master* (a meetup's own secret), *user's key*, *registration*.
- **Scope decisions:** a second exception to the freeze, for this. People who share one
  user's key can put each other out of a meetup (7).
- **Names:** `MOSH_MEETUP_KEY` holds a user's key (as it holds the shared key today);
  the meetup reads its master from `MOSH_MEETUP_MASTER`. Both become gosh's names with the rest
  (queue item 10).

## 11. Questions for the owner

1. A keyed meetup stops answering STUN (REGISTERED says where the end appears to be); an
   unkeyed one answers STUN as today and passes nothing. Agreed?
2. Two words become three: a meetup has a **master**, each user a **key**. Whoever runs a
   meetup makes keys for its users (`MOSH_MEETUP_MASTER=... gosh-server meetup-key`). Agreed?
