# Contributing

How to work in gosh, for a new teammate and for an agent, who both arrive knowing nothing
([SE1/A3](vendor/se1/guide/10-agents.md#a3)). Keep it short and true: a change that makes a line
here untrue updates it in the same change ([SE1/W11](vendor/se1/guide/08-work.md#w11)).

## What this is

gosh, a project of its own from 2026-11-13 (requirements v6): the code of `wmacevoy/mosh` at
`mosh-1.4.0-pf.7` migrated into named, tested parts. The old code, its build, its tests and the
record of how it came to be are in `../mosh-dev`; nothing is changed there.

## Run it, check it

**The full check:** `tools/check.sh`

**While working:** `tools/check.sh --lite [PART...]` builds, and runs the unit tests and outside tests of the parts of `src/` named (with none named: every part's unit tests and a terminal session). It leaves out the doctor, the fuzzers under AddressSanitizer, the reference and the NAT lab, and any fact a test marks as proved (`src/seam/proved.h`); it ends `PASS (lite: ...)` and says what it left out. It is never the whole check: a merge, CI and a release are held to `tools/check.sh` alone.

**With g++ 16, as C++26 with contracts** (branch `strong/gcc16`, 2026-11-18): `. tools/gcc16.sh`
first, then the same two commands. It fetches the compiler with pixi, without root, and sets `CC`
and `CXX`. The check passes that way on Debian 12, x86_64; nothing else has tried it, CI and the
releases do not use it, and no contract is written in gosh yet.

It prints PASS or says what failed, and CI runs the same command (`.github/workflows/ci.yml`). It
takes about three and a half minutes here, more from nothing. In order it checks that:

1. the repository is sound (`./se --doctor`: the working set, versioned citations, the queue);
2. the core is upstream's at the pinned commit, byte for byte (`REQ6/UP-2`);
3. it builds (`tools/build.sh`, into `.build/`: a copy of the core, the seam's patches applied
   to it, gosh's parts from `src/` copied in beside), and the parts' unit tests pass;
4. gosh and a stock mosh hold a terminal session, each way (`REQ10/SHAPE-1`, `tests/stock-session.py`);
5. the fork's own programs at pf.7, built in `.build/pf7` (`tools/build-pf7.sh`, which fetches the
   tag), pass the fork's tests that have been brought here (`tests/forward-e2e.py`,
   `tests/headless-smoke.py`, `tests/mocp.test`, `tests/wrapper.test`, two written here for what the
   fork tested only from inside, `tests/outage.py` and `tests/copy-cut.py`, and four cases of the NAT lab,
   `tests/nat-lab.py`, which needs `nft` and either root, user namespaces, or sudo);
6. gosh's own programs pass the tests of what they can do so far (`forward-e2e.py`, `outage.py`,
   `headless-smoke.py` with no relay and with the relay from the start, found when UDP is blocked, told a translated address, and left again when UDP returns, `mocp.test`, `copy-cut.py`, `gosh-command.py`, which goes from the `gosh` command line to a byte at a forward's target, and
   `gosh-stock.py`, for what `gosh` does with a host that has only a stock mosh),
   and a gosh end works with a pf.7 end each way. These are the tests gosh's parts must pass when they land.

**What is built today is upstream's mosh under gosh's names**, `gosh-client` and `gosh-server` in
`.build/bin`, with the seam applied (`seam/`, eight patches; `src/seam/`) and the fork's forwarding
command, stream and copy code carried over (`src/forwarding/`, `src/command/`, `src/copy/`). So
`gosh host` opens a session, `gosh-client` and `gosh-server` do forwards (`-L -R -LU -RU -N`),
`gosh --stdio` runs a command over a stream, and `gocp` copies files. Not yet: the relay,
path-finding and the meetup (queue items 7 and 8). `gosh` asks a host for
`gosh-server` and then `mosh-server`, and says what a stock one will not do (`REQ8/CMD-3`).

To set up: `git submodule update --init` for `core/`; a stock mosh 1.4.0 installed (Debian and
Ubuntu: the `mosh` package), since the check meets it; and the core's build needs
`protobuf-compiler libprotobuf-dev libncurses-dev libssl-dev zlib1g-dev` and autotools. Without
root, `tools/deps.sh` unpacks the first three under `.deps/` (Debian and Ubuntu only).

To reset: `rm -rf .build`. Nothing else holds state.

The doctor also asks GitHub whether the vendored guide is current: with no network it fails unless
this machine was able to ask in the last 30 days, so a fresh machine or CI runner needs the network.
The speed gate (`REQ14/SPEED-1`) is `tools/speed-gate.sh`. For each way (a TCP forward by UDP, a
copy over a stream, a TCP forward by the relay) it builds
the fastest version benchmarked so far, which `tools/fastest` names, and runs it turn and turn
about with the programs in `.build/bin`; it fails if gosh's rate is more than 10% under that
version's. It is not part of the check: it takes about 25 minutes. **It fails today for a
stream** (gosh at about three quarters of pf.7), which is what queue item 17 is for. It is run when a part lands,
and when it finds the new version the faster, that commit is recorded in `tools/fastest` in the
next change. Why it takes the runs it does is in `docs/FINDINGS.md` (2026-11-14).
The old code can still be run where it is: `../mosh-dev/tools/build.sh`, `../mosh-dev/tools/check.sh`.

`./se --doctor` says what this repository lacks. The start hook that runs `./se --post` is in
`.claude/settings.json`.

## Rules for this repository

- **No new features; push back on requests for them** (requirements, *Scope decisions*). Migrating,
  naming, testing and reshaping are not new features.
- **A part lands only pinned**: its tests that drive the programs from outside were first green
  against pf.7's programs, each was seen to fail, and the speed gate passes for it (`tools/speed-gate.sh`, `REQ14/SPEED-1`).
- **The core is not edited.** Upstream's code is carried at a named commit, untouched; what gosh
  needs changed there is the seam, and every change in it is tracked to that commit and listed in
  the design. A change that reshapes the core's code says where the code came from and why
  (`REQ10/SHAPE-1`, `REQ6/UP-2`).
- **Code and tests name the requirements they serve**, by versioned ID (`REQ1/CP-2`).
- **One line of copyright in each file, and the rest in two files.** Every source, test and tool
  of gosh's begins (after `#!`, where there is one) with: "Copyright 2026 Warren D. MacEvoy.
  GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH." Nothing longer:
  `LICENSE-GOSH` holds the notice, what it covers and where the files came from; `LICENSE-MOSH`
  holds mosh's own notice, for `core/` and for the text of mosh that the seam's patches change
  (the owner, 2026-11-14: "only core is old code", "a one-liner with a reference"). The owner
  holds the copyright in what an agent writes for gosh. `core/` and `vendor/` are never touched.
- **Never stop a process by name.** The machine this is developed on is also the owner's live mosh
  host and runs their private meetup. Stop what you started by process id.
- **Never run a test against the installed programs.** The fork's `nat-lab.py` ends by stopping
  every process on the machine whose program file is under the directory it was given; given
  `~/.local/bin`, that is the owner's live sessions. Tests get a build directory of their own.
  What the check and gosh's own tests stop is narrower still: a program under `.build/` that
  also carries this run's mark (`GOSH_TEST_RUN`) in its environment, so that two runs sharing
  `.build/` do not stop each other's. The
  stock mosh the check meets is the one exception: it is started by the test and stopped by its
  process id, and nothing else of it is touched.
- **Tests ask servers on the Internet nothing.** (The doctor's release check, above, is not a test
  of gosh and is the one thing in the check that does.)
- **The owner signs each commit and tags each release.** The agent's review of a part is accepted
  (requirements, *Review*); the documentation says "not independently reviewed" until the third
  party's review, which comes before a class uses gosh.
- **A change in a requirement's meaning is a new version** of the requirements, not an edit.

## Where things are

- **Code:** `core/` is upstream's mosh, a git submodule at the pinned commit, never edited.
  `seam/` is what gosh changes in it: patches the build applies, in order, to a copy
  (`seam/README.md`).
  `src/` holds gosh's parts, a directory each, named for the part, with a README and the part's
  unit tests; so far `src/forwarding/`, `src/seam/`, `src/command/`, `src/copy/`, `src/relay/`, `src/meetup/` and `src/pathfinding/`.
  A part with several kinds of thing in it has a subdirectory for each, with its own README and tests:
  `src/forwarding/tcp/`, `udp/` and `stream/`.
  **A unit test is named for the file it tests, with `-test`:** `connection-test.cc` for `connection.h`,
  `filecopy-test.cc` for `filecopy.cc` (the owner, 2026-11-14: "thisthing-test for thisthing", so that a test is
  found from the name of what it tests). A test the fork named otherwise is renamed when it is carried. `tools/` builds and checks; `tests/` holds tests that
  drive the programs from outside.
  **Unit tests are written with facts** (`vendor/facts/README.md`; the owner, 2026-11-17: "fix all the
  tests"): `FACTS( Name ) { ... }` groups, registered in order in `FACTS_REGISTER_ALL`, and
  `FACT_CHECK( a == b, "what it shows" )`, which prints both values when false and goes on.
  `--facts_plain` to read a run; `--facts_include=Name` to run one group. A value with no `operator<<`
  prints as well as facts can (a vector's elements, a pair's members, else `?`). A helper that checks
  takes `Facts* facts` as a parameter, or, where it cannot (a method, a callback), from `FactsCurrent()`.
  A false `FACT` leaves the whole group from any depth, by exception: so not in a destructor, and not
  under a `catch ( ... )` of the test's own. A comma in a template argument splits a fact: name the value first.
- **Working set:** `docs/`: `REQUIREMENTS_v16.md` (the one in force; v6 to v15 are beside it, and versions 1 to 5 are
  in `../mosh-dev/docs`), `QUEUE.md`, `FINDINGS.md`, and `DESIGN_v2.md` (the design in force; v1 is beside it, superseded,
  and still holds the seam file by file).
- **The guide:** `vendor/se1/`, release v1.7. Do not edit it; this file wins where they disagree.
- **Agents:** read `AGENTS.md` and this file before acting.

## What AGENTS.md's nouns mean here

| AGENTS.md says | In this repository |
| -------------- | ------------------ |
| the repository guide | `CONTRIBUTING.md` (this file) |
| the queue | `docs/QUEUE.md` |
| findings | `docs/FINDINGS.md` |
| the check command | `tools/check.sh` |
| the handoff check | none yet; the rule in AGENTS.md, *After a gap*, applies |
| `Non-goals`, `Scope decisions` | sections of `docs/REQUIREMENTS_v16.md` |
