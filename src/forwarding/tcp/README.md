# forwarding: TCP forwards

`-L` and `-R` (`REQ1/FWD-1`, `REQ1/FWD-3`), and the carrier of the stream: gosh's own TCP between
the two ends, each segment a datagram on the side channel. Part of forwarding
(`../README.md`); it uses forwarding's `forward` and `frame`, and nothing of `../udp/` or `../stream/`.

| File | What it holds | Test |
| --- | --- | --- |
| `connection.h` | gosh's TCP, the protocol: handshake, states, sending and acknowledgment, repair of losses, congestion, closing, and the table of connections. Logic | `connection-test.cc` |
| `bytes.h` | the byte queue a connection's two buffers are | `bytes-test.cc` |
| `options.h` | a segment's options, read: largest segment, window shift, selective acknowledgment permitted | `options-test.cc` |
| `held.h` | sending: what the other end says it holds beyond what it has acknowledged; the next gap to fill; what to pass over | `held-test.cc` |
| `early.h` | receiving: segments that came before their turn, and which ranges to say we hold | `early-test.cc` |
| `timer.h` | the round trip, and from it how long to wait before sending again | `timer-test.cc` |
| `tcpinudp.*` | a TCP segment as a compact datagram, both ways | `tcpinudp-test.cc` (the fork's) |
| `tcpadapter.*` | the sockets of forwarded TCP connections: listeners and targets. An edge | `tcpforward-test.cc` (the fork's, on real sockets) |
| `tcpforward.*` | TCP forwards and the stream, as segments over the side channel: frames each segment, refuses replays | the same |

`connection.h` was one file of 1,367 lines until 2026-11-14 (the owner: "too much to digest").
The five small files are what in it was arithmetic and not protocol: each is a plain value
with no socket, no clock and no `Out`, read and tested by itself. `held` and `early` are gosh's
words for what TCP calls the SACK scoreboard and the reassembly queue. Nothing they do changed:
the same statements, moved; with selective acknowledgment off the whole still agrees with the
fork's adapter over 5,000 scripted scenarios (the differential test, below).

**Where gosh's TCP differs from the fork's, on purpose.** Two things, both in `connection.h`:
selective acknowledgment (below), which an end that does not know it never sees; and what
happens to a reset that comes after the other end's finish (queue item 18, 2026-11-15). An end
closes and forgets a connection when both finishes are acknowledged, though the other end may
still hold bytes its application has not taken; anything that end then sends is answered with
a reset. The fork believed that reset and threw the held bytes away: up to 1 MB that had been
acknowledged, with no reordering needed, only a full buffer and a slow reader. gosh delivers
what it holds and then ends the connection (in order; or with a reset if the other end went
before it had all of ours). A reset before the other end's finish is believed as before.
**Not fixed:** when the slow reader is a pf.7 end, it still believes gosh's reset; that would
need the closed end to remember, for as long as an application may be slow.

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
it, and against one that says it holds what it never got. The fork's own `tcpforward-test.cc` runs against the edge and the logic
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
