# path-finding

A way straight between the two relays when both ends are behind NATs (`REQ1/PATH-3` to
`REQ1/PATH-7`; design v2, 2.2).

**Switched on** (2026-11-15). `punch.h` and `punch.cc` are the fork's at `mosh-1.4.0-pf.7`, from
`src/network/`, but at the places marked (gosh):

- **The meetup half is tickets** (`REQ15/PATH-4`, `REQ18/PATH-5`; `src/meetup/`). Given a meetup's
  address and a ticket (`use_meetup`), an end asks the meetup where it appears to be, registers
  from there, sealed under the ticket's key, again every 25 s, and sends through it as PASS. The
  initiator is end 0, the other end 1. Counters start from the time of day, so that a ticket used
  again by a new relay is not taken for an old one. The fork's shared key and copyable proof are
  gone.
- **A probe is opened only at its one length** (`PROBE_LEN`, 33 bytes). The fork handed any
  probe-marked datagram of up to 65,528 bytes to the cipher, whose 2,048-byte buffer is guarded by
  an `assert` alone: the fifth security fault (`docs/FINDINGS.md`). Seen red: without the check,
  `punch-test` aborts in that assert.
- **No STUN server unless named** (`REQ1/PATH-7`; `src/relay/settings.h`).
- **IPv4 and IPv6** (`REQ17/PATH-8`). One socket takes both families where the system allows
  (IPv4 addresses then come in IPv6's form, and `punch.cc` turns them back before the logic sees
  them); otherwise it is IPv4 alone. STUN reads IPv6 answers; candidates and this machine's own
  addresses are of both families (not an address of one link alone, which would need its
  interface named; nor, as text, `[fe80::...]`, which is refused). `[::ffff:a.b.c.d]:p` is the
  IPv4 address it is. While the way is straight by IPv4, the IPv6 candidates are still probed now
  and then, and a way by IPv6 is taken and kept while it answers: IPv6 first.
- **A way must work both ways** (2026-11-16, found by review: IPv6 that went one way only left
  both ends saying the way was open while nothing went either way, for good). Only an answer
  (`!`) to a probe of ours takes or moves the way; a question (`?`) from a new place is answered
  and asked back, so that its answer can; and a way by which our probes have gone unanswered for
  `ANSWERED_FOR` (7 s) is thought closed, whatever comes in by it. `finding-test.cc` has IPv6 one
  way only, a way that turns one-way once open, and an end moved where the other was not told.
  The relays say "the way through is now to ..." when it moves while open.

**Divided** (2026-11-15), as the design's three pieces:

| File | What it holds | Test |
| --- | --- | --- |
| `stun.h` | STUN, asking: the request; `stun_parse`, the edge that makes what `StunAnswer` kept a socket address; `a.b.c.d:p` as text. Logic | `stun-test.cc`, reading an answer the meetup's `answer.h` wrote |
| `finding.h` | `PathFinding`: candidates; probes sealed and numbered; which way is open; when to probe; the meetup registered with and passed through. Logic: the time and datagrams in, datagrams to send out | `finding-test.cc`: a network of a few lines, the meetup's own logic, time moved by the test |
| `punch.*` | `Punch`: the socket, this machine's addresses, the time of day, the process that asks the STUN servers. An edge, around `PathFinding`, with the interface the relays had | `punch-test.cc`, real sockets on loopback |

**Tested:** `finding-test.cc`: a way straight through, lost when nothing comes back for 3.5 s and
found again; a goodbye; a way through a meetup when nothing straight gets through, a datagram
through it, a way straight through still looked for and taken, and not turned back by a probe
heard through the meetup as well; an end moved by its NAT registering again; one moved before it
registers asking again where it is; a REGISTERED that names another address not taken; a forged
ticket; the probing every 250 ms for 8 s and then every 2 s; the oversized and short probe; a
probe copied and sent from elsewhere; a STUN answer giving a new place to be reached. Each guard
broken on purpose and seen red. `punch-test.cc` does the same with sockets;
`tests/headless-smoke.py ... punched` and `tests/relay-meetup.py` drive it through the relays.

**Not tested:** two machines behind real NATs or firewalls (the NAT lab against gosh, by either
family, and two machines in one room). The NAT lab (`tests/nat-lab.py`, the fork's) builds IPv4
networks only, and needs root to make them.

**Fuzzed** (queue item 21); `make check` replays each corpus and 200 seeded changes of each input,
and CI runs them under libFuzzer (`tools/fuzz.sh`):

- `stun-fuzzer.cc`, the fork's: STUN answers from anyone, and addresses as text, written back the
  same. Seen red under AddressSanitizer with an answer longer than it says allowed (`stun.h`);
  breaking the check of each attribute's length alone shows nothing (the whole answer's length is
  checked first).
- `finding-fuzzer.cc`, new: path-finding's logic given datagrams from anyone (the other end, the
  meetup, strangers, by IPv4 and IPv6) and real probes from a second end, as sent, changed or from
  elsewhere; it takes the session's datagrams only from where its way is. Seen red with a probe
  opened at any length: the corpus's `long-probe` reaches the cipher's assert, the fifth fault
  (FINDINGS, 2026-11-15). Since the review of item 21 (2026-11-16) both ends use a meetup, which
  the fuzzer plays with the meetup's own logic, so that the way through it is reached: by gcov of
  the corpus's replay, a REGISTERED taken 0 times before and 1,333 after, a GOODBYE 0 and 178,
  DATA through the meetup 0 and 164 (`through-meetup`).

The check also replays both under AddressSanitizer (`tools/asan-replay.sh`). Seen: with `stun.h`
letting an attribute run 8 bytes past the body, the plain replay passes and that one stops on a
heap-buffer-overflow (the reviewer's input, `attr-past-body`).
