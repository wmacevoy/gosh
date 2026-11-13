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

It prints PASS or says what failed, and CI runs the same command (`.github/workflows/ci.yml`).
**There is no code yet, so today it checks only that the repository is sound** (`./se --doctor`:
the working set, versioned citations, the queue). Queue item 3 adds the build and the tests to it.
The doctor also asks GitHub whether the vendored guide is current: with no network it fails unless
this machine was able to ask in the last 30 days, so a fresh machine or CI runner needs the network.
Until then a claim about gosh's behaviour cannot be checked here
([SE1/A2](vendor/se1/guide/10-agents.md#a2)); what can be run is the old code:
`../mosh-dev/tools/build.sh`, `../mosh-dev/tools/check.sh`, and `../mosh-dev/tools/speed.py` for
the figure `REQ6/SPEED-1` compares against.

`./se --doctor` says what this repository lacks. The start hook that runs `./se --post` is in
`.claude/settings.json`.

## Rules for this repository

- **No new features; push back on requests for them** (requirements, *Scope decisions*). Migrating,
  naming, testing and reshaping are not new features.
- **A part lands only pinned**: its tests that drive the programs from outside were first green
  against pf.7's programs, each was seen to fail, and `tools/speed.py`'s figure for it is not more than 10% under pf.7's (`REQ6/SPEED-1`).
- **The core is not edited.** Upstream's code is carried at a named commit, untouched; what gosh
  needs changed there is the seam, and every change in it is tracked to that commit and listed in
  the design. A change that reshapes the core's code says where the code came from and why
  (`REQ9/SHAPE-1`, `REQ6/UP-2`).
- **Code and tests name the requirements they serve**, by versioned ID (`REQ1/CP-2`).
- **Every file that came from mosh or the fork keeps its copyright notice.** gosh is GPL-3.
- **Never stop a process by name.** The machine this is developed on is also the owner's live mosh
  host and runs their private meetup. Stop what you started by process id.
- **Never run a test against the installed programs.** The fork's `nat-lab.py` ends by stopping
  every process on the machine whose program file is under the directory it was given; given
  `~/.local/bin`, that is the owner's live sessions. Tests get a build directory of their own.
- **Tests ask servers on the Internet nothing.** (The doctor's release check, above, is not a test
  of gosh and is the one thing in the check that does.)
- **The owner signs each commit and tags each release.** The agent's review of a part is accepted
  (requirements, *Review*); the documentation says "not independently reviewed" until the third
  party's review, which comes before a class uses gosh.
- **A change in a requirement's meaning is a new version** of the requirements, not an edit.

## Where things are

- **Working set:** `docs/`: `REQUIREMENTS_v9.md` (the one in force; v6 to v8 are beside it, and versions 1 to 5 are
  in `../mosh-dev/docs`), `QUEUE.md`, `FINDINGS.md`, and `DESIGN_v1.md`.
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
| `Non-goals`, `Scope decisions` | sections of `docs/REQUIREMENTS_v9.md` |
