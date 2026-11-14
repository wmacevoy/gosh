# forwarding

Forwards and the stream: TCP and UDP ports, and one byte channel, carried over the side channel
(`REQ1/FWD-1`, `REQ1/FWD-2`, `REQ1/FWD-3`, `REQ1/FWD-4`; design v2, 2.2).

**Carried, and being reshaped** (queue items 5, 15, 16). `connection.h` is the first piece in its
new shape: see below. Every other source file here is the fork's at
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
| `old-tcpadapter.*` | the fork's adapter, unchanged but for its name: for the differential test only |
| `tcpinudp.*` | a TCP segment as a compact datagram, both ways |

## connection (the new shape)

`connection.h` is gosh's TCP as **logic**: the algorithm of the fork's `AdapterPort`, statement
for statement, with its sockets, clock and random numbers taken out. It is told what happened,
with the time and what the session can do just now, and says what to do through a type its
caller supplies (the edge in the programs, a recorder in tests). `tcpadapter.*` is that **edge**:
it owns the sockets, reads the clock and draws sequence numbers, and decides nothing. The
programs run both. `old-tcpadapter.*` is the fork's adapter as it was, under another class name,
linked only into `connection-diff`; it goes when forwarding has landed.

| Test | What it shows |
| --- | --- |
| `connection-diff.cc` | the new logic and the fork's adapter given the same scripted scenario (writes, losses, repeats, time passing, contact lost and found, resets) emit the same segments and deliver the same bytes at every step. 500 scenarios in the check; `./connection-diff 20000 1000` for more |
| `connection-test.cc` | the logic alone, for what a real socket pair never does: a slow application, a target that refuses or never answers, the sixty-fifth connection, and six hours out of contact (`REQ1/FWD-3`) |

The logic's two buffers are a byte queue (`Gosh::Bytes`, in `connection.h`) and not strings: the
fork moved its send buffer down on every acknowledgment, which was about 40% of a forward's
processor time (`docs/FINDINGS.md`, 2026-11-14).

Both were held to account by breaking the logic on purpose: of 32 breaks tried, the two tests
together miss one, which changes nothing that can be seen (queue item 15).
