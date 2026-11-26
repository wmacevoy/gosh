# forwarding: UDP forwards

`-LU` and `-RU` (`REQ1/FWD-2`): datagrams carried as they are, or in fragments with parity when
they do not fit. Part of forwarding (`../README.md`); it uses forwarding's `forward` and
`frame`, and nothing of `../tcp/` or `../stream/`.

| File | What it holds | Test |
| --- | --- | --- |
| `conversations.h` | which conversation a datagram is of: the table, its numbers, forgetting, pushing out the quietest. Logic | `conversations-test.cc` |
| `sending.h` | a datagram on its way out: one payload, or fragments with parity when asked or worth it. Logic | `sending-test.cc` |
| `receiving.h` | what arrives, made into whole datagrams: the replay window, datagrams in pieces, the ones already rebuilt. Logic | `receiving-test.cc` |
| `../../rules.h` | the numbers and words the three share: of forwarding's ABI since 2026-11-26, as `forwarding.h` names them | — |
| `fragments.*` | a datagram cut into pieces with parity, and rebuilt; whether parity is worth it. Logic (the fork's text) | `udpforward-test.cc` |
| `udpforward.*` | the sockets of UDP forwards: listeners and targets. An edge (the class keeps the fork's name and public face) | `udpforward-test.cc` (the fork's, on real sockets) |

`conversations.h` was one file of 423 lines until 2026-11-15 (the owner: "too much to digest").
Sending and receiving share nothing but the frame, so each is a file; the table is what ties a
datagram to a socket. The same statements, moved; the tests that were there pass unchanged.

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
