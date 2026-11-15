# Proposal: a meetup that knows who it is passing for

**A proposal, not the design in force.** Drafted by the agent on 2026-11-15 at the owner's word:
"pf7 was a step to here. let it go. go for a tailscale. (or talescale-like if that breaks some
ownership)". It replaces how a meetup decides whom to pass datagrams for
(`src/meetup/`, queue item 8). Nothing here is built yet. An independent review of this text
comes before any code ([SE1/W4](../vendor/se1/guide/08-work.md#w4): a wire format with keys in
it is a one-way door once released).

## 1. What is wrong today, in one paragraph

A meetup has one key, shared by everyone allowed to use it. To have a datagram passed on, an
end puts twelve bytes in front of it: four random bytes and a tag of those four bytes. The
bytes travel in the clear, say nothing of who sent them, when, or for which pair, and the
meetup treats whoever last named a pair as one of its ends. So anyone who sees one datagram
can use the meetup for ever and can take an end's place (`src/meetup/README.md`;
`passing-test.cc`, `test_replay`, with the real cipher).

## 2. What Tailscale's relays do, and what is taken from them

From the agent's memory of Tailscale's published design, not checked against its source, and
**none of its code or wire format is used** (so no question of whose it is arises):

1. nothing reusable crosses the wire in the clear;
2. each end proves, each time it connects, that it holds a key: seeing the exchange gives an
   onlooker nothing to use later;
3. the relay passes a datagram to an identity, not to whoever spoke last.

Tailscale gets these from TLS and a public key for every node. gosh has neither, and
`SHAPE-3` forbids adding a dependency a bare machine lacks: the only cipher gosh can count on
everywhere is the one mosh already carries (AES-OCB, a shared-key cipher that seals and proves
in one pass). **This proposal gets the three properties with that cipher alone.** Section 7
says what that costs against real public keys, and why it is still recommended.

## 3. The keys

| Key | Who holds it | Made by | Used for |
| --- | --- | --- | --- |
| the meetup's key, `Km` | the meetup, and every user allowed to use it | `gosh-server meetup-key`, as today | an end shows it holds `Km` when it registers |
| the pair's secret, `V` | the two ends of one session, and (once they register) the meetup | each end, from the secret the two relays already share to seal their probes | names the pair to the meetup, secretly |
| a registration's key, `Kr` | one end and the meetup | the meetup, at random, for each registration | proves each datagram comes from that registration |

`V` is 16 bytes: the tag of sealing a fixed text under the pair's probe key (as the pair's name
is made today, `punch.cc:195-197`), but it is **never sent in the clear**.

## 4. The protocol

All of it is UDP, to the meetup's one port, as today. Two kinds of datagram.

### 4.1 Registering (one round trip)

```
end -> meetup    REGISTER   mark(8) | nonce(8) | sealed under Km with that nonce:
                                         { version(1), role(1), time(8), V(16) }
meetup -> end    REGISTERED mark(8) | nonce(8) | sealed under Km with that nonce:
                                         { echo of the end's nonce(8), id(8), Kr(16),
                                           where the end appears to be: address(4), port(2) }
```

- `role` is 0 for the end that made the pair's key and 1 for the other. Path-finding already
  has this (`initiator`).
- `time` is the end's clock, in seconds. The meetup takes a REGISTER only if it is within 120 s
  of its own clock, and only once: it remembers the nonces it has taken for 240 s.
- The seal is the proof: only a holder of `Km` can make a REGISTER the meetup can open. One
  that does not open, is stale, or repeats, gets **no answer at all**, so a stranger cannot
  tell that a meetup is there (`REQ1/PATH-5` as it stands).
- The two directions use nonces that cannot collide (the top bit says which way), since both
  are sealed under `Km`.
- The meetup then holds a **registration**: `(V, role)` at the address the REGISTER came from,
  with key `Kr` and number `id`. A later REGISTER for the same `(V, role)` replaces it (the end
  moved, or started again).
- The answer tells the end where it appears to be, which is what it asked STUN for. A meetup
  with a key no longer answers STUN at all.

### 4.2 Passing a datagram on

```
end -> meetup    PASS       mark(8) | id(8) | counter(8) | tag(8) | the datagram
meetup -> end    PASSED     mark(8) | the datagram
```

- `tag` is the first 8 bytes of the tag of sealing nothing under `Kr` with `counter` as the
  nonce. One block of cipher work at each end.
- The meetup passes it on only if: `id` is a registration; the datagram came from that
  registration's address; `counter` has not been seen (a window of 1,024, as elsewhere in
  gosh); and the tag is right. Then it sends PASSED to the registration `(V, the other role)`,
  at **its** registered address. If there is none, or it has not been heard for two minutes,
  the datagram is dropped.
- Nothing is passed to an address because it "spoke last". An end that moves registers again.

### 4.3 What the meetup forgets

A registration not used for two minutes; and, at 8,192 registrations, a new one is refused
(4,096 pairs, as today).

## 5. What an attacker can do, case by case

| Attacker | Today | Proposed |
| --- | --- | --- |
| Sees traffic, has no key, anywhere on the Internet | Uses the meetup for ever; takes an end's place | Nothing. A copied REGISTER is refused (seen already); a copied PASS is refused (counter seen; wrong address) |
| Sees traffic **and can forge the sender's address** on the path, no key | As above | Can replay nothing useful: every PASS counter is spent when the true one arrives. Can win a race only by delaying the true datagram, and one who can delay can drop, which is theirs to do anyway |
| Sees a REGISTER and replays it before the true one arrives (the true one being lost) | — | The meetup registers the end at the attacker's address. The attacker cannot read `Kr` (it is sealed under `Km`) and so cannot pass anything; the end hears no answer, registers again with a new nonce, and takes its place back. A short denial |
| Holds `Km` (another user of the same meetup), **not** on the path | Uses the meetup (allowed); can take an end's place if it learns the pair's name, which is in the clear | Uses the meetup (allowed). Cannot touch another pair: it does not know `V`, which is never in the clear |
| Holds `Km` **and** sees the pair's traffic (a classmate on the same Wi-Fi) | Takes an end's place | Can open the REGISTER, learn `V`, and register as that end: **takes an end's place**. Not closed: see section 7 |
| The meetup itself | Sees which addresses talk and how much; can drop | The same. It also holds `V`, which is no use except at that meetup |

In every row, nobody reads or changes what the two ends say: that is sealed by the session.

## 6. Each input: absent, late, wrong, lying

- **Absent.** No REGISTERED comes back: the end tries again (a new nonce each time), slowly,
  and goes on with whatever other paths it has; a meetup is one way among several. The other
  end has not registered: datagrams are dropped, as today.
- **Late.** A REGISTER more than 120 s from the meetup's clock is refused, silently. **This
  rests on the clocks of the ends and the meetup agreeing to within two minutes**, which is an
  assumption to be written into the requirements. An end whose clock is wrong cannot use a
  keyed meetup and is not told why by the meetup; the relay's log says that no meetup answered.
- **Wrong.** Too short, wrong mark, unknown version, a seal that does not open, an unknown
  `id`, a tag that is wrong: dropped, unanswered.
- **Lying.** A REGISTER with a true seal and a false `V` or `role` makes a registration nobody
  pairs with (or, for one who knows `V`, is the last row but two above). A PASS from the wrong
  address is dropped even with a right tag.

## 7. What this does not do, and the alternative

**Not closed: users of one meetup are not protected from each other on a shared network.**
Whoever holds `Km` and can see a pair's REGISTER can take an end's place at the meetup. They
gain a denial and the sight of sealed datagrams, nothing more. Closing it needs each end to
prove something the other key-holders cannot learn, which a shared-key cipher cannot give.

**The alternative: a public key for every end, as Tailscale has.** Each end would make a key
pair for the session, the two ends would exchange the public halves over ssh (as they exchange
the probe key today), and each would prove its private half to the meetup. That closes the
row above. Its cost: a public-key primitive. OpenSSL and nettle have one (X25519); Apple's
library that mosh can also be built with does not. It would be the first thing in gosh that a
supported build of mosh's own cipher cannot provide, which `SHAPE-3` says no to as it stands.

**Recommendation: the shared-key design above.** It closes everything an outsider could do,
which is the finding. What is left is a denial between people who were all given the same
key, on the same network; and it is said plainly. If the owner wants that closed too, it is a
`SHAPE-3` exception to decide, not a detail.

## 8. What changes in gosh

- `src/meetup/`: `proof.*` goes (nothing shows a key by twelve bytes any more). New logic, each
  with a test of numbers and no socket: `register.h` (make and open REGISTER and REGISTERED;
  the clock rule; the nonces taken), `passing.h` (registrations by `(V, role)`; the counter
  window; where to send). `serve.cc` stays the edge.
- `src/pathfinding/`: the end's half of the same (register, keep `id`, `Kr` and a counter,
  wrap each datagram), as logic, when that part is reshaped (the rest of item 8).
- The attack tests written on 2026-11-15 stay, turned round: each thing the onlooker could do
  becomes a check that they cannot.
- `tests/meetup-answers.py`: a meetup with a key says nothing to a stranger (as now), and
  nothing to a REGISTER that is copied, stale, or sealed with another key.
- **Wire:** a gosh meetup and a pf.7 relay no longer understand each other (and the reverse).
  The requirements already say gosh does not work with the fork's releases at the other end
  (*Non-goals*, since v6). The NAT lab's `passed` case, which runs pf.7's programs against
  pf.7's meetup, is unaffected until gosh's own programs run in the lab.

## 9. What the requirements would say (for a v15)

- **PATH-5**, reworded: *A meetup given a key answers nothing, and passes nothing on, for
  anyone who has not shown it the key afresh; and what it passes for a pair goes only to that
  pair's two ends. A datagram copied off the network is no use to whoever copied it.* *(judged
  in part: "no use" is argued in the design and tested for each case the design lists.)*
- **Scope decisions:** a second exception to the freeze, for this. And: users of one meetup,
  on one network, can put each other out of it (section 7), accepted or not.
- **Assumptions:** the clocks of the two ends and the meetup agree to within two minutes.

## 10. Questions for the owner

1. Shared-key design as recommended, or public keys with a `SHAPE-3` exception (section 7)?
2. Is "within two minutes" acceptable for the clocks (section 6)?
3. A keyed meetup stops answering STUN altogether (the registration's answer says where the
   end appears to be). An unkeyed one answers STUN as today and passes nothing. Agreed?
