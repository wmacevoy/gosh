# forwarding: the stream

One reliable byte channel between a process at each end (`-X`; `REQ1/CP-7`). Part of forwarding
(`../README.md`). These files include nothing of `../tcp/` or `../udp/`: the stream is carried as
TCP's forward 0, and it is `../forwarding.h` that puts the two together.

| File | What it holds |
| --- | --- |
| `stream.h` | when the client of a stream's session is finished, as logic (new shape); `stream-test.cc` |
| `streamchild.*` | a stream's process: starts what is at each end of a stream, as the program says (since 2026-11-17 it knows no application: `src/seam/applications.*` names them). The fork's, carried, less the applications; an edge |

Unit test: `stream-test.cc`.

## the stream

A stream is one reliable byte channel between a process at each end of a session (`-X`;
`REQ1/CP-7`, and what `gocp` rides on). It is forward 0 of a kind of its own: a connection with no
listening port, between a socket the client already has and one the server makes when asked. So
`connection.h` carries it with nothing added.

- `streamchild.*` starts the process at each end, and makes it what the program gives it: a
  function of the socket, run in the new process. A stream's words say which (`stream_words`
  splits them), but what each name is, forwarding does not know: `@copy …` (one end of a file
  copy), `@stdio`, `@sh COMMAND` and `@connect HOST PORT` (`-W`) are named in `../seam/applications.*`, so that the part that
  carries knows no application (the layers, `../README.md`; until 2026-11-17 this file included
  copy and ran it). **Carried, not reshaped**: it forks and reads `HOME`.
- `stream.h` is the one decision, as logic: the client is finished when its process has ended and
  either nothing of the stream is left or five seconds have passed.
- The seam joins them to the session (`../seam/hooks.h`): the client starts its process once the
  session is up; the server gives forwarding a function that starts its own when the stream's
  connection arrives. Forwarding decides when, never what.
