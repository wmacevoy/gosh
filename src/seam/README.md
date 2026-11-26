# the seam

Every place gosh's code meets the core (`REQ10/SHAPE-1`; design v2, section 4). It has two halves:

- **`../../seam/*.patch`**: what gosh changes *in* the core, applied by the build to a copy.
- **This directory**: what those changes call.

**Laid out** (2026-11-26; `../DOMAINS`): its ABI is the headers here, in `Gosh::Seam`, and the patches call only it (`Gosh::Seam::headless()`, `Gosh::Seam::turn()`, ...); its inside is `imp/`, in `Gosh::Seam::Imp`.

| File | Kind | What it holds |
| --- | --- | --- |
| **ABI** (in `Gosh::Seam`: what the core's patches and every part's fuzzers call) | | |
| `core-main.h` | edge | the core's two mains under the names the seam's patch gives them |
| `hooks.h` | edge | the few functions the core's loops call to give gosh's parts their turn |
| `asked.h` | edge | what else the core asks of gosh where the patches call it: whether there is no terminal, whether this is a stream's session, whether a process may yet be started, and a stream's exit status (from `session.h` and `streaming.h`, 2026-11-26) |
| `fuzz-replay.h` | test | every part's fuzzer's `main` for the ordinary compiler: replays its corpus, and 200 changes of each input from a seeded generator, so `make check` fuzzes a little without clang; keeps an input that crashes. Here because the build copies only `src/`'s directories into the core's tree (queue item 21) |
| `proved.h`, `gosh-version.h` | test, build | a fact a test does not try because it is proved; the version `--version` says |
| **inside** (`imp/`, in `Gosh::Seam::Imp`) | | |
| `options.*` | logic | takes gosh's words (`-L -R -LU -RU -F -N -X`) out of a program's arguments, and says from its name which program gosh-client is to be; `options-test.cc` is its test |
| `client-main.cc`, `server-main.cc` | edge | each program's `main`: takes gosh's words out, then runs the core's `main` with the words a stock mosh would have been given. The client's first decides which program it is (the command, the client, or ssh's proxy) |
| `session.*` | edge | the process's one session of gosh's: what was asked for, forwarding, and the state below |
| `streaming.*` | edge | a stream: what the client keeps of the process at its end, and the look it takes at it each turn |
| `relaying.*` | edge | the relay: what the client keeps of its local relay; starting it, and starting another if it dies (when is `src/relay/start.h`'s to say) |

The hooks decide nothing. They hold forwarding, hand its descriptors and timers to the program's
loop, say `connected; forwarding.` for a client with no terminal (`REQ1/FWD-5`), and for a stream
(`REQ1/CP-7`) start the client's process, watch for its end, and give its status as the client's.

Not here yet: path-finding. A part adds words to
`options` and, where it needs a turn in the loop, a line to a hook and a file of its own beside it.

`hooks.h` held all four of these until 2026-11-15 (queue item 19): every part that needed a
turn in the loop added its state and its functions to one header. Now a part that needs a turn
gets a file of its own here, and `hooks.h` only calls it: a stream's process is started, given
up on, waited for and asked for its status in `streaming.cc` alone (since 2026-11-15; the review of
item 19 found `hooks.h` still doing some of it). These are edges (they fork, wait and
write to standard error) and have no unit tests of their own: the tests that drive the
programs from outside are their tests (`forward-e2e.py`, `headless-smoke.py`, `mocp.test`,
`relay-dies.py`).
