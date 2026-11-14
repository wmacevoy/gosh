# the seam

Every place gosh's code meets the core (`REQ10/SHAPE-1`; design v2, section 4). It has two halves:

- **`../../seam/*.patch`**: what gosh changes *in* the core, applied by the build to a copy.
- **This directory**: what those changes call.

| File | Kind | What it holds |
| --- | --- | --- |
| `options.*` | logic | takes gosh's words (`-L -R -LU -RU -F -N -X`) out of a program's arguments, and says from its name which program gosh-client is to be; `options-test.cc` is its test |
| `client-main.cc`, `server-main.cc` | edge | each program's `main`: takes gosh's words out, then runs the core's `main` with the words a stock mosh would have been given. The client's first decides which program it is (the command, the client, or ssh's proxy) |
| `core-main.h` | edge | the core's two mains under the names the seam's patch gives them |
| `hooks.h`, `session.cc` | edge | the process's one session of gosh's, and the few functions the core's loops call to give gosh's parts their turn |

The hooks decide nothing. They hold forwarding, hand its descriptors and timers to the program's
loop, say `connected; forwarding.` for a client with no terminal (`REQ1/FWD-5`), and for a stream
(`REQ1/CP-7`) start the client's process, watch for its end, and give its status as the client's.

Not here yet: relay. Each adds words to
`options` and, where it needs a turn in the loop, a line to a hook.
