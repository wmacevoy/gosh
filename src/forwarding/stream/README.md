# forwarding: the stream

One reliable byte channel between a process at each end (`-X`; `REQ1/CP-7`). Part of forwarding
(`../README.md`). These files include nothing of `../tcp/` or `../udp/`: the stream is carried as
TCP's forward 0, and it is `../forwarding.h` that puts the two together.

| File | What it holds |
| --- | --- |
| `stream.h` | when the client of a stream's session is finished, as logic (new shape); `stream-test.cc` |
| `streamchild.*` | a stream's process: starts what is at each end of a stream (`@copy`, `@stdio`, `@sh`). The fork's, carried with two include lines changed; an edge |

Unit test: `stream-test.cc`.

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
