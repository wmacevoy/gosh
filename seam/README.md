# The seam

What gosh changes in the core, tracked to the pin (`REQ21/SHAPE-1`). `core/` is upstream's mosh at
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

**The core is factored into domains** (`REQ21/SHAPE-1`), the cipher first (patch 12). A move is a
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
