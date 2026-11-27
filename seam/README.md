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
