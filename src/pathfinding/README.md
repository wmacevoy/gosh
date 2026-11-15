# path-finding

A way straight between the two relays when both ends are behind NATs (`REQ1/PATH-3` to
`REQ1/PATH-7`; design v2, 2.2).

**Switched on** (2026-11-15). `punch.h` and `punch.cc` are the fork's at `mosh-1.4.0-pf.7`, from
`src/network/`, but at the places marked (gosh):

- **The meetup half is tickets** (`REQ15/PATH-4`, `REQ15/PATH-5`; `src/meetup/`). Given a meetup's
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

**Divided** (2026-11-15), as the design's three pieces:

| File | What it holds | Test |
| --- | --- | --- |
| `stun.h` | STUN, asking: the request, an answer read, `a.b.c.d:p` as text. Logic | `stun-test.cc`, reading an answer the meetup's `answer.h` wrote |
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

**Not tested:** two machines behind real NATs (the NAT lab against gosh, and two machines in one
room).
