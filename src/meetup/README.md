# meetup

A machine with a public address where the two ends of a session, each behind its NAT, find
each other; and through which, with its key, their datagrams are passed (`REQ15/PATH-4`,
`REQ15/PATH-5`; design v2, 2.2). `gosh-server meetup PORT`, with its key in `MOSH_MEETUP_KEY`;
`gosh-server meetup-key` makes one.

It calls nothing of gosh's other parts. Path-finding calls its `proof.h`, to show the key.

**Landed with queue item 8** (2026-11-15). The fork had this as one function, `meetup_serve`,
in `src/network/punch.cc`, with the proof beside it. Here:

| File | What it holds | Test |
| --- | --- | --- |
| `proof.*` | the twelve bytes that show the key: made, and checked. Logic (the fork's text) | `proof-test.cc`, with the real cipher |
| `passing.h` | datagrams passed on between the two ends of a pair: which end, where to, how long remembered. Logic | `passing-test.cc`, numbers for time, no socket |
| `answer.h` | the STUN question answered, and no more than 200 a second. Logic | `answer-test.cc` |
| `serve.cc`, `meetup.h` | the process: one UDP socket and the clock. An edge | `tests/meetup-answers.py`, from outside, against pf.7's and gosh's |
| `nacl.*` | the one thing that calls the carried TweetNaCl (`vendor/tweetnacl/`): a box, a signature, a hash, with every size checked. Logic, but for making keys and random bytes. **Built for the meetup that is proposed; nothing here uses it yet** | `nacl-test.cc` |

## What the proof does not show: a seen proof can be replayed

Found by reading on 2026-11-13; shown on 2026-11-15, with the real cipher, in `proof-test.cc`
and `passing-test.cc` (`test_replay`). **It is not fixed.**

The proof is four random bytes and a tag of those four bytes alone. Nothing in it is a time, a
sender, or a pair of ends, and it travels in the clear at the front of every datagram a meetup
is asked to pass on. So someone on the path who sees one such datagram, and does not have the
key, can from then on, from anywhere:

1. have the meetup pass datagrams between addresses of their own, for a pair of their own
   naming: the meetup is theirs to use;
2. have a datagram of theirs delivered to an end of the pair they saw (that end's session
   refuses it: it is not sealed by the other end);
3. take the place of one end of that pair, so that what the other end sends is passed to them
   and not to its true end, until the true end sends again; and by going on doing it, keep an
   end out.

They cannot read or change anything the two ends say to each other, which the ends seal.
As the requirement stood until v14, a meetup "passes nothing on, for anyone who does not show
the key"; a stranger with no proof is refused (tested), and an onlooker with a copied proof is
not. `REQ15/PATH-5`, reworded on 2026-11-15 because of this, asks for what this code does not
do yet.

What is to be done about it is decided (the owner, 2026-11-15: "go for a tailscale"; "meetup
keeps a list of signature authorities, public keys are signed by an authority, and a
blacklist"): `docs/PROPOSAL_meetup-registration.md`, revision 4, twice attacked on paper.
It is not built. Until it is, this part is the fork's meetup, and should not be relied on by
anyone who minds the three things above.

## Not tested

A holder of the key being answered and passed on **by the process** (the rule and the cipher
are tested; the two together through a socket are tested only against pf.7, by the NAT lab's
`passed` case, until path-finding lands and gosh's own programs can run in the lab).
