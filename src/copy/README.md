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

**Mended after the review of copy** (2026-11-16), the fork's faults each, each with a test seen
red first:

- `--delete` into `/` removed what it had just been told to keep: a name was joined as `//sub`
  and never matched `/sub`. `names.h`'s `child_of`, tested in `names-test.cc`; the edge's joins use
  it. The edge's use is read, not driven: a test would need `/` as its destination.
- A file at the source named as another's part file (`.x.mocp-part` beside `x`) was taken for
  that file's part, and lost, with both ends saying it was copied. Now the receiver refuses such a
  name, that file alone (`is_part_name`), and the other is copied whole.
- A file the sender could not open for one run had it start from 0, so the receiver threw away
  the start it held. Now it starts where it was asked, and the start survives.
- The receiver keeps whether a file is arriving apart from whether its part could be opened, so
  a part that cannot be opened fails that file alone (no more `/dev/null` in its place); a LIST or
  LIST_END once the round's plan is made is not the protocol.
- `filecopy-test.cc` plays a lying other end over a socket pair, and the real end hangs up at once
  with status 1, for: DATA or FILE_END with no file begun; ROUND_END with one still arriving; a
  second LIST_END, or a LIST after it; a RESULT for something never listed; the same file wanted
  twice. Each check was broken on purpose and its case went red.
- The part is synced to disk before it takes its name, for the envelope's "Interruption": read,
  not tested (it needs a power cut).

**Not tested, or not changed:** the receiving end checks the names it is given but opens and
renames through whatever is already at the destination, a symbolic link included
(`docs/FINDINGS.md`, 2026-11-13; bears on `REQ1/CP-6`). The edge's own lines (progress, the
`--auto` wait, `umask`) are tested only through `filecopy-test.cc` and the outside tests. A part
that cannot be opened is tested by a directory closed to writing, not by running out of
descriptors. The review's lesser points are left: a sender may list without limit (memory), and a
stall of a whole interval at one point of the `--auto` wait could make it wait for ever.
