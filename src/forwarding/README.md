# forwarding

Forwards and the stream: TCP and UDP ports, and one byte channel, carried over the side channel
(`REQ1/FWD-1`, `REQ1/FWD-2`, `REQ1/FWD-3`, `REQ1/FWD-4`; design v2, 2.2).

**Carried, and being reshaped** (queue items 5, 15, 16). `tcp/connection.h`, `tcp/tcpadapter.*`, `udp/conversations.h` and `udp/udpforward.*` are in
their new shape: see their READMEs. Every other source file here is the fork's at
`mosh-1.4.0-pf.7`, from `src/network/` (the three tests from `src/tests/`, where they were `udp-forward.cc`, `tcp-forward.cc` and `tcp-in-udp.cc`; renamed on 2026-11-14 so that a file's test is the file's name and `-test`), with nothing changed
but the `#include` lines that name the files that moved with it. Since item 16 began, three
pieces that were already logic stand in files of their own, in gosh's namespace, the text of
each function unchanged (compared line for line); the error for a bad specification no longer
says "UDP", since TCP forwards use it too; and `UDPForwarder::encode` and `decode`, which only
tests called, are in the two tests that call them. One exception: `udpforward-test.cc`
has lost three tests that are other parts' (`test_relay_frames`, `test_punch`,
`test_loss_measurement`); they come back with relay, path-finding and the seam.

So what `REQ6/SHAPE-5` asks of a part is not yet true here: these files read the clock and make
their own system calls (design v1, *Edges*). Queue items 15 and 16 reshape them, with this text
to test the new against.

| File | What it holds |
| --- | --- |
| `forwarding.h` | every forward of a session behind one event-loop interface; gives each arriving payload to its kind (a switch; the fork's `SideChannelMux` is gone from the seam) |
| `forward.*` | one forward's specification, `[bind:]port:host:hostport`: logic (the fork's text, moved out of `udpforward` at item 16) |
| `frame.*` | the 13-byte header every forwarded payload has, and the replay window: logic (likewise) |
| `tcp/` | TCP forwards (`-L`, `-R`), and what carries the stream: gosh's TCP as logic, its sockets, a segment as a datagram |
| `udp/` | UDP forwards (`-LU`, `-RU`): conversations as logic, fragments, their sockets |
| `stream/` | the stream (`-X`): when its client is finished, and the process at each end |

Each subdirectory has a README and its unit tests (a test is the file's name and `-test`). The
three include nothing of each other; each uses only `forward` and `frame` here, and
`forwarding.h` is the one place that knows all three. Divided this way on 2026-11-14 at the
owner's word ("forwarding seems a little too pooled"): subdirectories and not parts, so that
forwarding stays the one part the requirements' glossary names. It builds as one library.

**Fuzzed** (queue item 21), each with its corpus beside it (`NAME-fuzzer-corpus/`); `make check`
replays each corpus and 200 seeded changes of each input, and CI runs them under libFuzzer
(`tools/fuzz.sh`). The fork's four, carried; `fuzz.h` is the fork's `forward_fuzz.h`. Each seen red
with a guard broken on purpose:

| Fuzzer | What it holds forwarding to | Seen red with |
| --- | --- | --- |
| `frame-fuzzer.cc` | frames, reassembly with parity, the replay window | the window taking a number it has seen (`frame.cc`). A frame too short for its header (the guard in `decode_frame`) was not found by the replay's small search: libFuzzer's to find |
| `tcp/tcpinudp-fuzzer.cc` | a segment read in each form is written back the same | options longer than the segment (`tcpinudp.cc`): an exception, then abort. Breaking the first length check alone shows nothing: the header-length check after it refuses the same inputs |
| `tcp/tcpadapter-fuzzer.cc` | the adapter, both roles, sends no segment larger than the link takes | the segment's header left out of what fits (`connection.h`'s `local_mss`). The fork's corpus never filled a segment; `connect-full-segments` does (2026-11-16). Breaking the clamp in either of its two places alone shows nothing |
| `udp/udpforward-fuzzer.cc` | the forwarder sends nothing larger than the link takes, and holds no more conversations or reassemblies than its limits | a datagram too big to go whole sent whole (`udp/sending.h`). The limit of 64 reassemblies is beyond what its inputs reach |
