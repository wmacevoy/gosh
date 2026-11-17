# gosh's parts, and their layers

Each directory here is one part, named by the glossary's word for what it does (`REQ6/SHAPE-5`);
each has a README. The core, upstream mosh, is beside them in `../core/`, untouched (`REQ10/SHAPE-1`).
The build copies each part into its copy of the core (`../tools/build.sh`).

Every part sits in one of four layers. **Only glue reaches across layers**; every other part
includes only parts of its own layer, and the core. `../tests/layers.py` fails the check
otherwise.

| Layer | Parts | What it does |
| --- | --- | --- |
| **application** | the terminal (the core's `terminal/` and `statesync/`), `copy/` | what the user came for: a screen kept up to date, files copied |
| **carrying** | `forwarding/` (the stream, TCP, UDP) | puts an application's bytes into the session's datagrams, beside the terminal's, and back out |
| **paths** | `relay/`, `pathfinding/`, `meetup/` | gets the session's datagrams to the other end: straight by UDP, through ssh, by a punched way, or through a meetup |
| **glue** | `command/`, `seam/` | starts the right processes with the right words, and joins the parts to each other and to the core |

The session, the core's sealed datagrams, is in the middle: carrying rides inside it, paths carry
it, and applications ride on carrying. So none of the three needs another, and a new application
(a further stream, the deferred `-D`) touches its own part and the glue, not forwarding.

Two of the seam's files are tooling, not glue, and any part may include them:
`seam/fuzz-replay.h` (the fuzzers' replay `main`) and `seam/gosh-version.h` (written by the build).

**Where the applications meet carrying:** a stream's process (`forwarding/stream/streamchild.*`)
runs whatever the program gives it; `seam/applications.*` names each application a stream can be
between (`@copy`, `@stdio`, `@sh`) and says what it becomes. Until 2026-11-17 forwarding included
copy and named the three itself, the one crossing of the layers.

*(The layer names are the owner's choice of 2026-11-17, on the agent's proposal: "yes, do 1-3 with
your names". Directories were not regrouped by layer: an `app/` holding `copy/` alone, with the
terminal staying in the core, would label more than it separates.)*
