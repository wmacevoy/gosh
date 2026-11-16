# copy

One end of a file copy over a stream: the sender says what it has, the receiver says what of it
it lacks and how much of each it already holds, and the sender sends the rest (`REQ1/CP-1` to
`REQ1/CP-6`; design v2, 2.2). A file takes its own name only when it is whole and its checksum
agrees.

**Reshaped** (queue item 9, 2026-11-16). The fork's `filecopy.cc` (`mosh-1.4.0-pf.7`,
`src/network/`) was one file of 1,363 lines holding two blocking procedures and everything they
decide. Here what they decide is logic, in five headers each with its test, and what is left is
the edge: the two procedures, which ask the logic and do what it says. Every format is pf.7's, byte
for byte (`mocp-1`, `.NAME.mocp-part`), so a gosh end copies with a pf.7 end. In the check: a
file copied each way between a gosh end and a pf.7 end (`tests/headless-smoke.py`), and a copy cut
off and carried on from its part across them, each way (`tests/copy-cut.py`); and
`tests/mocp.test`, `tests/copy-cut.py` and `tests/copy-liar.py` against each alone.

| File | What it holds | Test |
| --- | --- | --- |
| `options.h` | the words: `send`/`receive`, `-r --auto --delete --glob -q --interval=MS`, `--`, the paths. Logic | `options-test.cc` (`REQ1/CP-1`, `REQ1/CP-5`) |
| `messages.h` | the thirteen messages of `mocp-1`, their framing, writing and reading; a listed name that is not safe is Malformed. Logic | `messages-test.cc`, the bytes spelled out (`REQ1/CP-1`, `REQ1/CP-6`) |
| `names.h` | is a name safe; where it lands; its part file; the name a source goes by; `~`. Logic | `names-test.cc` (`REQ1/CP-6`, `REQ1/CP-3`) |
| `checksum.h` | 128 bits, not cryptographic. Logic | `checksum-test.cc`, against values worked out apart from this code (`REQ1/CP-2`, `REQ1/CP-3`) |
| `decisions.h` | what the sender offers each round; where it carries on from; whether a file changed while read; whether the receiver has a file, is in the way, holds a usable start; whether what arrived is whole and as sent; what is kept when removing. Logic, given what `stat` said | `decisions-test.cc` (`REQ1/CP-2` to `REQ1/CP-5`) |
| `filecopy.*` | the edge: the stream (`Channel`), the file system, the user's messages, and `run`, which reads `HOME` once and passes it and the clock in | `filecopy-test.cc`, the fork's: two ends in two processes over a socket pair, on real files |

It calls nothing else of gosh's. A stream's process (in `../forwarding/`) becomes one end of a
copy by calling `FileCopy::run`; the command writes the words that say which end.

**Seen red:** each guard in the five headers was broken on purpose, one at a time, and its test
failed (2026-11-16). Two cannot be: `safe_relative_path`'s refusals of an empty name and of an
absolute one are also made by its refusal of an empty part, so breaking either alone changes
nothing. Two, a WANT or a FILE_BEGIN for an index past the list, read past the end of a vector when
broken; their tests notice that only under AddressSanitizer.

**Not tested, or not changed:** the receiving end checks the names it is given but opens and
renames through whatever is already at the destination, a symbolic link included
(`docs/FINDINGS.md`, 2026-11-13; bears on `REQ1/CP-6`). The edge's own lines (progress, the
`--auto` wait, `umask`) are tested only through `filecopy-test.cc` and the outside tests.

**Fuzzed** (queue item 21): `filecopy-fuzzer.cc`, the fork's fuzzer of the receiving end, with its
corpus (`filecopy-fuzzer-corpus/`): whatever the sending end says, a file beside the destination is
untouched. `make check` replays the corpus and 200 seeded changes of each input; CI runs it under
libFuzzer (`tools/fuzz.sh`). Seen red: with `..` allowed in a name (`names.h`), it aborts.
