# the command

`gosh` and `gocp`: log in by ssh, start the server, and return the client's arguments, so that the
same process carries on as the client (`REQ1/CMD-1`, `REQ6/NAME-1`; design v2, 2.2). gosh-client
is the command when it is run under either name, or with either as its first word.

**Carried, not yet reshaped** (queue items 5 and 10). `moshwrapper.*` is the fork's at
`mosh-1.4.0-pf.7`, from `src/frontend/`, with one `#include` line changed. So:

- it reads and sets the environment in the middle of its work (design v1, *Edges*);
- its usage text and messages still say `mosh` and `mocp`, and its help has leftover Perl in it
  (`docs/FINDINGS.md`, 2026-11-13); the names change in one go with queue item 10;
- it writes words for copy, the stream and relay (`--copy`, `--stdio`, `--relay`) that gosh's
  programs do not take yet: only a terminal session and the forwards work through it so far;
- it asks a host for `mosh-server` and nothing else. `REQ8/CMD-3` (ask for `gosh-server` first,
  and say what a stock server will not do) is not built yet.
