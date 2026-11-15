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

**Tested:** `punch-test.cc`, with real sockets on loopback and the meetup's own logic in the
same process: a way straight through; a way through a meetup when neither end knows where the
other is; a forged ticket refused; the oversized probe. `tests/headless-smoke.py ... punched` and
`tests/relay-meetup.py` drive it through the relays.

**Not tested:** two machines behind real NATs (the NAT lab against gosh, and two machines in one
room); the logic is not yet divided from the sockets (queue item 8).
