# relay

The session carried over an ssh connection when UDP cannot get there (`REQ1/PATH-1`,
`REQ1/PATH-2`; design v2, 2.2), between a local relay on the user's machine and a remote relay
(`gosh-server relay ADDRESS PORT`) on the server's.

**Carried, and to be reshaped** (queue item 7). `relay.h` and `relay.cc` are the fork's at
`mosh-1.4.0-pf.7`, from `src/network/`, with nothing changed but two `#include` lines. So what
`REQ6/SHAPE-5` asks is not yet true of them: they read the clock and five environment variables
and make their own system calls. The reshape takes the decisions out as logic (`paths`, the
frame) with a unit test that drives every path change with numbers.

| File | What it holds |
| --- | --- |
| `start.h` | when the client starts its relay: logic (new shape); `start-test.cc` |
| `relay.*` | both relays, their framing and their words to each other: the fork's, carried |

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

**Not tested:** the relay's own timings (2.5 s, 10 s, 60 s, the back-off) are pinned by no test
yet; that the local relay dying is noticed (`docs/FINDINGS.md`, 2026-11-13: it is not); OpenBSD,
where the client must keep the right to start a process.
