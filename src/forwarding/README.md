# forwarding

Forwards and the stream: TCP and UDP ports, and one byte channel, carried over the side channel
(`REQ1/FWD-1`, `REQ1/FWD-2`, `REQ1/FWD-3`, `REQ1/FWD-4`; design v2, 2.2).

**Carried, and being reshaped** (queue items 5, 15, 16). `connection.h` and `tcpadapter.*` are in
their new shape: see below. Every other source file here is the fork's at
`mosh-1.4.0-pf.7`, from `src/network/` (the three tests from `src/tests/`), with nothing changed
but the `#include` lines that name the files that moved with it. One exception: `udp-forward.cc`
has lost three tests that are other parts' (`test_relay_frames`, `test_punch`,
`test_loss_measurement`); they come back with relay, path-finding and the seam.

So what `REQ6/SHAPE-5` asks of a part is not yet true here: these files read the clock and make
their own system calls (design v1, *Edges*). Queue items 15 and 16 reshape them, with this text
to test the new against.

| File | What it holds |
| --- | --- |
| `forwarding.h` | every forward of a session behind one event-loop interface |
| `udpforward.*` | UDP forwards; and the frame, the replay window and the forward's specification, which TCP uses too |
| `tcpforward.*` | TCP forwards and the stream, as segments over the side channel |
| `connection.h` | gosh's TCP: one end of a forwarded connection, as logic (new shape) |
| `tcpadapter.*` | the sockets of forwarded TCP connections: listeners and targets, an edge (new shape) |
| `tcpinudp.*` | a TCP segment as a compact datagram, both ways |
| `stream.h` | when the client of a stream's session is finished, as logic (new shape); `stream-test.cc` |
| `streamchild.*` | a stream's process: starts what is at each end of a stream (`@copy`, `@stdio`, `@sh`). The fork's, carried with two include lines changed; an edge |

## connection (the new shape)

`connection.h` is gosh's TCP as **logic**: the algorithm of the fork's `AdapterPort`, statement
for statement, with its sockets, clock and random numbers taken out. It is told what happened,
with the time and what the session can do just now, and says what to do through a type its
caller supplies (the edge in the programs, a recorder in tests). `tcpadapter.*` is that **edge**:
it owns the sockets, reads the clock and draws sequence numbers, and decides nothing. Landed on
2026-11-14 (queue item 15).

Its two buffers are a byte queue (`Gosh::Bytes`, in `connection.h`) and not strings: the fork
moved its send buffer down on every acknowledgment, which was about 40% of a forward's processor
time (`docs/FINDINGS.md`, 2026-11-14).

It has one thing the fork's did not: **selective acknowledgment** (RFC 2018; requirements v14,
queue item 17). The listening end asks for it in its SYN and the other end agrees if it knows
it; then an end holding segments beyond a gap says which ranges it holds on each acknowledgment
(up to four, the latest arrival's first), and the sender keeps those ranges, sends again only
the gaps between them, one for each acknowledgment that says another segment got there, and
passes over them when a timer sends it back to the start. A range the acknowledgments reach
and stop inside is not believed. An end that does not know it (pf.7) never agrees, is never
sent a range, and gets the fork's TCP unchanged: `Connections( listening, false )` is that, and
is what the differential test below was run against.

`connection-test.cc` is the logic alone, with numbers for time and a recorder for what it asks:
transfers, a slow application, windows, handshakes, limits, two connections, six hours out
of contact (`REQ1/FWD-3`), and segments lost on the way: how many are then sent again and in how
many exchanges, with selective acknowledgment and without, against an end that does not know
it, and against one that says it holds what it never got. The fork's own `tcp-forward.cc` runs against the edge and the logic
together, on real sockets.

**What is no longer here.** While the logic was being taken out, `connection-diff.cc` gave it and
the fork's adapter the same scripted scenarios and compared every segment (20,000 scenarios, 6.8
million segments, no difference). It needed the fork's class, which was deleted when the part
landed, so it went too; both are in this repository's history at commit `2001e4f`. Two kinds of
deliberate break were caught only by that test and are caught by nothing here now: leaving loss
recovery, and giving up on one-at-a-time repair after many losses. **Before changing the logic,
bring the test back from that commit** and run `./connection-diff 20000 1000`. That was done for
selective acknowledgment on 2026-11-14, with `connections( listening, false )` in the test: old
and new agreed over 2,000 scenarios of 600 steps, 418,252 segments.

## the stream

A stream is one reliable byte channel between a process at each end of a session (`-X`;
`REQ1/CP-7`, and what `gocp` rides on). It is forward 0 of a kind of its own: a connection with no
listening port, between a socket the client already has and one the server makes when asked. So
`connection.h` carries it with nothing added.

- `streamchild.*` starts the process at each end. A stream's words say what: `@copy …` (it becomes
  one end of a file copy: `../copy/`), `@stdio` (this program's own input and output), `@sh
  COMMAND`. **Carried, not reshaped**: it forks, execs and reads `HOME` and `SHELL`.
- `stream.h` is the one decision, as logic: the client is finished when its process has ended and
  either nothing of the stream is left or five seconds have passed.
- The seam joins them to the session (`../seam/hooks.h`): the client starts its process once the
  session is up; the server gives forwarding a function that starts its own when the stream's
  connection arrives. Forwarding decides when, never what.
