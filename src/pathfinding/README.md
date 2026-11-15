# path-finding

A way straight between the two relays when both ends are behind NATs (`REQ1/PATH-3` to
`REQ1/PATH-7`; design v2, 2.2).

**Carried only, and switched off** (queue item 8 lands it). `punch.h` and `punch.cc` are the
fork's at `mosh-1.4.0-pf.7`, from `src/network/`, with one `#include` line changed. They are
here because the relay's file builds against them. (The meetup, which was in the same file in
the fork, is its own part since 2026-11-15: `src/meetup/`.) gosh starts its relay with path-finding off
(`src/relay/README.md`), so none of this runs, and none of it is tested against gosh.
