# relay

The session carried over an ssh connection when UDP cannot get there (`REQ1/PATH-1`,
`REQ1/PATH-2`; design v2, 2.2), between a local relay on the user's machine and a remote relay
(`gosh-server relay ADDRESS PORT`) on the server's.

**Reshaped** (queue item 7). The fork's `relay.cc` (`mosh-1.4.0-pf.7`, `src/network/`) was one
file of 876 lines holding two processes and everything they decide. Here what they decide is
logic, in four small files each with its test, and what is left is the two processes, a file
each, which ask the logic at places marked `(gosh)` and do what it says. Their text is otherwise
the fork's. `local.cc` reads the environment in one place; `remote.cc` reads what ssh says of
the connection, and whether path-finding is off; both read the name of the log.

| File | What it holds |
| --- | --- |
| `start.h` | when the client starts its relay: logic (new shape); `start-test.cc` |
| `paths.h` | every change of path the two relays make: logic (new shape); `paths-test.cc` |
| `frame.h` | a datagram on the ssh stream (two bytes of length before it), and the relays' words to each other: logic; `frame-test.cc` (its first test is the fork's) |
| `settings.h` | what the environment tells the relays, as one value: idle time, path-finding, whom to ask, the key; and the addresses in what ssh says of a connection: logic; `settings-test.cc` |
| `whose.h` | whose a datagram is: only the server's address counts as the server answering; the client is a socket of this user's; the remote relay keeps its likelier ways to the server: logic; `whose-test.cc` |
| `local.cc` | the local relay: a process on the user's machine, between the client and the server by UDP and by an ssh connection it starts. An edge |
| `remote.cc` | the remote relay: `gosh-server relay ADDRESS PORT`, which ssh starts beside the server. An edge |
| `process.h` | what both need as processes and decide nothing with: the log, the mark on their words, signals, the clock, a pipe, a UDP socket |
| `relay.h` | what the seam calls: start the local relay; be the remote relay |

Who calls it: the seam. gosh's client `main` reads how the relay is started (`MOSH_RELAY`,
`MOSH_RELAY_COMMAND`, which the command writes); the seam's hooks ask `start.h` each turn of
the client's loop and start the local relay once; gosh's server `main` becomes the remote relay
when its first word is `relay`.

**Path-finding is off.** `local.cc` and `remote.cc` build against `src/pathfinding/punch.*`, and every use of
it is behind a test for whether there is one. Until queue item 8 lands, both relays are started
with `MOSH_PUNCH=never` whatever the environment says: the local one then never looks, and the
remote one never says the `HELLO` that invites a local relay to (a pf.7 client's would,
otherwise). So no path-finding code runs at either end and no server on the Internet is asked
anything.

**Tested** (in `tools/check.sh`): `tests/headless-smoke.py` with the relay from the start
(`always`), found when UDP gets no answer (`blocked`, by IPv4 and IPv6), with the server's
address not what the relay was told (`translated`), and going back to UDP when it works again
(`returned`); each against pf.7 and against gosh, and `always` with a gosh end and a pf.7 end
each way. Seen red: a client that never asks `start.h` fails `blocked`.

`paths-test.cc` pins the timings with numbers and no socket: 2.5 s, 10 s, a minute, the
back-off (1, 2, 4 ... 30 s; up to 5 minutes while the stream has never worked), the remote
relay's 3 s and its minute. It was written against the fork's rules before the relays' code was
changed to ask `paths.h`; sixteen deliberate breaks of a rule are each noticed.

**Where gosh's relay differs from the fork's, on purpose** (2026-11-15; `docs/FINDINGS.md`). The
fork's relays changed where they sent things because of datagrams anyone could have sent. Four
changes, at places marked `(gosh)` in `local.cc` and `remote.cc`:

1. The remote relay tries several addresses for the server. The fork stayed with whichever
   answered first, so a user of the server's machine who bound the free loopback port took the
   path. gosh gives up only the ways less likely than the one that answered.
2. The local relay took any datagram on its direct socket for the server answering, and stopped
   using ssh. gosh takes only one from the server's address. `tests/relay-spoof.py` is this
   attack, from outside: red against pf.7, green against gosh.
3. The local relay took whoever last sent to its loopback port for the client. gosh takes a new
   sender only if the kernel's list of UDP sockets says it is a socket of the same user. **On a
   system without `/proc/net/udp` (not Linux) it cannot tell, and behaves as the fork did.**
4. One byte from ssh reset the wait between attempts to start it. gosh resets it when a whole
   frame has arrived.

Of these, 2 is shown from outside; 1, 3 and 4 are tested as rules (`whose-test.cc`,
`paths-test.cc`) and their few lines at the edge are read, not driven by a test: 1 is a race
to demonstrate, and 3 needs a second user.

**Not tested:** that the edge does what `paths.h` says is tested only by the smoke test's four
cases. Nothing in the check makes the local relay try a fresh socket, back off from an ssh that
fails or asks for a password, stop a real ssh (the remote relay is run directly), fill its
backlog, or be killed outright; `MOSH_RELAY=never` with a command is tested only as a rule. That the local relay dying is noticed (`docs/FINDINGS.md`, 2026-11-13: it is not); OpenBSD,
where the client must keep the right to start a process.
