# relay

The session carried over an ssh connection when UDP cannot get there (`REQ1/PATH-1`,
`REQ1/PATH-2`; design v2, 2.2), between a local relay on the user's machine and a remote relay
(`gosh-server relay ADDRESS PORT`) on the server's.

**Carried, and being reshaped** (queue item 7). `relay.h` and `relay.cc` are the fork's at
`mosh-1.4.0-pf.7`, from `src/network/`. Since 2026-11-14 the decisions about paths are out of
`relay.cc` and in `paths.h`: which way a datagram goes, when the ssh stream is started, started
again and let go, when a fresh socket is tried, and at the remote relay which way an answer
goes back and when it leaves. `relay.cc` asks `paths.h` at the places marked `(gosh)` and does
what it says. The framing and the relays' words are `frame.h`; what the environment tells the
relays is made sense of in `settings.h`. What is left in `relay.cc` is the two relays as
processes: sockets, the ssh child, the clock, `getenv` in one place for the local relay and one
line each for the log and for what ssh says of the connection, and its dealings with
path-finding, which are item 8's. That text is the fork's.

| File | What it holds |
| --- | --- |
| `start.h` | when the client starts its relay: logic (new shape); `start-test.cc` |
| `paths.h` | every change of path the two relays make: logic (new shape); `paths-test.cc` |
| `frame.h` | a datagram on the ssh stream (two bytes of length before it), and the relays' words to each other: logic; `frame-test.cc` (its first test is the fork's) |
| `settings.h` | what the environment tells the relays, as one value: idle time, path-finding, whom to ask, the key; and the addresses in what ssh says of a connection: logic; `settings-test.cc` |
| `relay.*` | both relays as processes, their framing and their words to each other: the fork's, an edge of `paths.h` |

Who calls it: the seam. gosh's client `main` reads how the relay is started (`MOSH_RELAY`,
`MOSH_RELAY_COMMAND`, which the command writes); the seam's hooks ask `start.h` each turn of
the client's loop and start the local relay once; gosh's server `main` becomes the remote relay
when its first word is `relay`.

**Path-finding is off.** `relay.cc` builds against `src/pathfinding/punch.*`, and every use of
it is behind a test for whether there is one. Until queue item 8 lands, the seam starts the
relay with `MOSH_PUNCH=never` whatever the environment says, so no path-finding code runs and
no server on the Internet is asked anything.

**Tested** (in `tools/check.sh`): `tests/headless-smoke.py` with the relay from the start
(`always`), found when UDP gets no answer (`blocked`, by IPv4 and IPv6), with the server's
address not what the relay was told (`translated`), and going back to UDP when it works again
(`returned`); each against pf.7 and against gosh, and `always` with a gosh end and a pf.7 end
each way. Seen red: a client that never asks `start.h` fails `blocked`.

`paths-test.cc` pins the timings with numbers and no socket: 2.5 s, 10 s, a minute, the
back-off (1, 2, 4 ... 30 s; up to 5 minutes while the stream has never worked), the remote
relay's 3 s and its minute. It was written against the fork's rules before `relay.cc` was
changed to ask `paths.h`; sixteen deliberate breaks of a rule are each noticed.

**Not tested:** that the edge does what `paths.h` says is tested only by the smoke test's four
cases; that the local relay dying is noticed (`docs/FINDINGS.md`, 2026-11-13: it is not); OpenBSD,
where the client must keep the right to start a process.
