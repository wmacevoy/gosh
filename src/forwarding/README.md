# forwarding

Forwards and the stream: TCP and UDP ports, and one byte channel, carried over the side channel
(`REQ1/FWD-1`, `REQ1/FWD-2`, `REQ1/FWD-3`, `REQ1/FWD-4`; design v2, 2.2).

**Carried, not yet reshaped** (queue item 5). Every source file here is the fork's at
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
| `tcpadapter.*` | a TCP of gosh's own joined to ordinary sockets: one end of a connection |
| `tcpinudp.*` | a TCP segment as a compact datagram, both ways |
