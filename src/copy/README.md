# copy

One end of a file copy over a stream: the sender says what it has, the receiver says what of it
it lacks and how much of each it already holds, and the sender sends the rest (`REQ1/CP-1` to
`REQ1/CP-6`; design v2, 2.2). A file takes its own name only when it is whole and its checksum
agrees.

**Carried, not yet reshaped** (queue items 6 and 9). `filecopy.*` is the fork's at
`mosh-1.4.0-pf.7`, from `src/network/`, and `file-copy.cc` its test from `src/tests/`, each with
one `#include` line changed. So what `REQ6/SHAPE-5` asks is not yet true here: its logic is two
blocking procedures that read and write the stream, the clock and the file system from several
depths, and it reads `HOME` at each use (design v1, *Edges*). Item 9 reshapes it.

It calls nothing else of gosh's. A stream's process (in `../forwarding/`) becomes one end of a
copy by calling `FileCopy::run`; the command writes the words that say which end.

Known, and not yet tested or changed: the receiving end checks the names it is given but opens
and renames through whatever is already at the destination, a symbolic link included
(`docs/FINDINGS.md`, 2026-11-13; bears on `REQ1/CP-6`).
