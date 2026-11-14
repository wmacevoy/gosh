# forwarding: UDP forwards

`-LU` and `-RU` (`REQ1/FWD-2`): datagrams carried as they are, or in fragments with parity when
they do not fit. Part of forwarding (`../README.md`); it uses forwarding's `forward` and
`frame`, and nothing of `../tcp/` or `../stream/`.

| File | What it holds |
| --- | --- |
| `fragments.*` | a datagram cut into pieces with parity, and rebuilt; whether parity is worth it: logic (likewise) |
| `conversations.h` | UDP forwards as logic: which conversation a datagram is of, its number, fragments on the way out and in, replay, and forgetting (new shape); `conversations-test.cc` |
| `udpforward.*` | the sockets of UDP forwards: listeners and targets, an edge (new shape; the class keeps the fork's name and public face) |

Unit tests: `conversations-test.cc` (logic alone), `udpforward-test.cc` (the fork's, on real sockets).

## conversations (the new shape)

`conversations.h` is what the fork's `UDPForwarder` decided, statement for statement, with its
sockets and clock taken out: it is told that a datagram came from an application or a target,
or a payload from the other end, with the time and what the session can do just now, and says
what to send, open, close and deliver through a type its caller supplies. `udpforward.*` is
that caller in the programs: it binds, resolves, reads and writes, and decides nothing. Landed
in queue item 16.

`conversations-test.cc` is the logic alone with numbers for time: there and back, what is
refused (a replay, another kind, no such forward, no socket, the session not ready), forgetting
at two minutes and a millisecond, the sixty-fifth conversation, fragments with a chosen one
lost, and when parity is added. Each was seen red with the logic broken. The fork's
`udpforward-test.cc` still runs against the edge and the logic together, on real sockets.
