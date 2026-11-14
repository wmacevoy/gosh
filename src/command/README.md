# the command

`gosh` and `gocp`: log in by ssh, start the server, and return the client's arguments, so that the
same process carries on as the client (`REQ1/CMD-1`, `REQ6/NAME-1`; design v2, 2.2). gosh-client
is the command when it is run under either name, or with either as its first word.

**Carried, not yet reshaped** (queue items 5 and 10). `moshwrapper.*` is the fork's at
`mosh-1.4.0-pf.7`, from `src/frontend/`, changed in two ways: one `#include` line; and, for
`REQ8/CMD-3`, six places in its login, each marked `(gosh, REQ8/CMD-3)`, that call `stock.*`,
and one marked `(gosh, REQ1/FWD-5)` that keeps a login's chatter off `-N`'s standard output.
`stock.*` is new and is logic only, with `stock-test.cc`: the line that picks the host's server,
and what is said when it is a stock mosh's. For the rest:

- it reads and sets the environment in the middle of its work (design v1, *Edges*);
- its usage text and messages still say `mosh` and `mocp`, and its help has leftover Perl in it
  (`docs/FINDINGS.md`, 2026-11-13); the names change in one go with queue item 10;
- it writes words for relay (`--relay`) that gosh's programs do not take yet. A terminal session,
  the forwards, `--stdio` and copies (`gocp`) work through it;
- its ssh fallback still names `mosh-server` on the host (`--relay`; relay is queue item 7).
