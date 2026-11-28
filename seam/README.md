# The seam

What gosh changes in the core, tracked to the pin (`REQ22/SHAPE-1`). `core/` is upstream's mosh at
its pinned commit and is never edited. `tools/build.sh` copies it to `.build/core` and applies
every `*.patch` here to the copy, in the order of their names. A patch that does not apply
exactly stops the build.

Each patch begins by saying what it is for (by requirement), what it changes, where in the core,
whether it only adds or also reshapes, and which patches it needs before it. They are a series:
each is cut against the result of the one before, and they share lines, so they cannot be
reordered or dropped singly.

To change one: edit the copy in `.build/core`, see it build and the check pass, and cut the patch
again from the difference. To move the pin: move `core/`, build, and repair the first patch that
no longer applies.

**The core is factored into domains** (`REQ22/SHAPE-1`), the cipher first (patch 12). A move is a
git rename inside a patch (`diff --git`, `rename from`, `rename to`), which GNU patch applies as it
applies the rest: the patch's own lines say where each file came from at the pin, its hunks hold only
what changed in the file, and no new build step carries it (the client, 2026-11-27: "use git renames
in the patches"). To cut one: in a copy of the core with the patches before it applied and
committed, `git mv`, edit, and `git diff -M` against that commit. GNU patch 2.7 reads renames; that
macOS's `patch` does is not yet seen (the release's macOS job builds by it).

`DOMAINS` here is the core's map, as `../src/DOMAINS` is gosh's: the lint reads the program as built
(`.build/core`), by the rule a domain alone (`tools/proofs.sh`), and refuses a reach into a domain's
inside that the map does not allow. A domain of the core not yet laid out is named with `| *`
(every header of it its ABI), and held only to the other domains' boundaries; gosh's parts are one
such domain there, for `../src/DOMAINS` holds each of them to its own.

Patch 13 moves the cipher's tests into it (`src/crypto/imp/*-test.cc`), and the whole check runs
them, as it runs each laid-out core domain's tests (`tools/check.sh`): before, gosh's check ran none
of the core's unit tests.

Patch 14 lays out the core's transport (`src/network`): its inside under `imp/` in `Network::Imp`,
and no header of it says `using namespace`.

Patch 15 lays out state synchronisation (`src/statesync`) as `StateSync`: until then its two
objects were declared in other domains' namespaces, the terminal's (`Terminal::Complete`) and the
transport's (`Network::UserStream`), as mosh named them.

Patch 16 lays out the terminal (`src/terminal`): its parser is `Terminal::Parser` (until then a
namespace of its own beside `Terminal`), and the parser's states, the dispatcher and the user's
input are its inside, in `Terminal::Imp`.

Patch 17 lays out the programs (`src/frontend`) as `Frontend`: its ABI is the client's
predictions (`terminaloverlay.h`, in `Frontend::Overlay`, which no longer says `using namespace`
of the terminal and the transport); the two mains, `STMClient` (in `Frontend::Imp`) and the rest
are its inside.

Patch 18 lays out the utilities (`src/util`) as `Util`: until then none of it was in a namespace.
Its macros (`fatal_assert`, `dos_assert`) keep their names, and call `Util::`; `pty_compat`'s
stand-ins for `forkpty` and `cfmakeraw` stay in no namespace, as the system's are, until patch 21 (`DOMAINS`
allows them). Left named and not laid out: what is generated (`src/include`, `src/protobufs`),
and the core's examples, fuzzers and tests.

Patch 19 gives each of the core's laid-out domains a test of its own (the transport, state sync,
the terminal, the programs, the utilities), of the behaviour the factoring must not change; the
check runs them with the cipher's.

Patch 20 puts OCB's interface (`src/crypto/imp/ae.h`, Krovetz's, in C) in `Crypto::Imp`, and its
definitions with it: until then it was in no namespace, and every user of the cipher's ABI saw it,
since `crypto.h` includes it for one private field. Its functions keep C linkage, so the symbols
linked are the same; `DOMAINS` no longer allows it.

Patch 21 makes the pseudo-terminal a domain, `Pty` (`src/pty`): `pty_compat`'s stand-ins for
`forkpty` and `cfmakeraw`, where a system lacks them, moved out of the utilities and into a
namespace (the owner, 2026-11-27: "Pty as its own domain"). It is the operating system's side of a
terminal; `src/terminal` is the emulator, and makes no system call. `DOMAINS` no longer allows the
two in no namespace. Pty has no test of its own: where the system has both functions, as on Linux
and macOS, it compiles to nothing.

Patch 22 adds to the cipher's ABI, as v22 allows: `Base64Key::of_octets`, a key from its 16
octets as they are. gosh's ticket (`src/meetup/ticket.h`) makes its keys so, where it wrote the
bytes as base64 with the cipher's inside for the key to read back; `DOMAINS` no longer allows that,
and allows nothing now.

Patch 23 carries by a type a bound the cipher's callers already keep, as v22 allows:
`Session::decrypt` takes its length as a `Session::Length`, a `Strong::bounded<0, RECEIVE_MTU + 8,
size_t>` (the nonce's 8 bytes, and a body of at most the buffer's 2048) (gosh's `src/strong/bounded.h`), so that its `int` arithmetic is proved free of overflow
for every length it may be handed. A datagram shorter than 24 bytes still throws, since anyone may
send one (the owner, 2026-11-28). Code that is not proved comes in by `decrypt_checked`, which tests
the length and ends the process past 2056, where `decrypt`'s assert ended it before; the network
receives into 2048 bytes, so for it the test never fails. The cipher's header includes gosh's
`bounded.h`: an ABI header of gosh's, the one place the core names a part of gosh.
