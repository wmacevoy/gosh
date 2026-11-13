# Handoff — 2026-11-13

From the session that ran in `../mosh-dev` to the first session started here. **This note is
recalled, not observed** ([SE1/A11](vendor/se1/guide/10-agents.md#a11)): each line names where to
check it, and where the two disagree the source wins. It ages; delete it once queue item 1 is
under way.

## What gosh is, in three lines

- The owner decided on 2026-11-13 that the fork `wmacevoy/mosh` becomes a project of its own, gosh,
  migrated part by part into "comprehensible tested parts", with no new features.
  *Check:* `docs/REQUIREMENTS_v6.md`, *What changed from v5* and *Scope decisions*; the owner's
  words are quoted there.
- The fork is frozen at `mosh-1.4.0-pf.7` and is what gosh is measured against. Nothing is changed
  there. *Check:* `git -C ../mosh-dev/mosh describe --tags master`.
- There is no code here, no design, and no check command. *Check:* `ls`; `./se --doctor`.

## Start here

1. Read `CONTRIBUTING.md`, then `docs/REQUIREMENTS_v6.md` (glossary, shape, scope decisions,
   non-goals), then `docs/QUEUE.md`.
2. Do **queue item 1, the design**. Its inputs are in the old workspace:
   `../mosh-dev/docs/DESIGN_v1.md` (components, state, one-way doors, decisions) and the fork's
   changes to upstream's own files, which become the seam:
   `git -C ../mosh-dev/mosh diff decd9b7 mosh-1.4.0-pf.7 --stat -- src` (`decd9b7` is upstream's
   commit the fork was cut from).
3. Item 2, the model kept with viki, waits on item 1's part names. The tools are `../viki` and
   `../viki-agent`; start with `../viki-agent/GUIDE.md`.

## Decisions the owner made, and where each is written

All in `docs/REQUIREMENTS_v6.md` unless it says otherwise. Do not act on this list; open the source.

- upstream's code is carried untouched at a pinned commit; gosh's changes to it are a listed seam
  (SHAPE-1, UP-2);
- gosh must work with a stock mosh at the other end, and need not with the fork's releases;
- the names are `gosh`, `gocp`, `gosh-client`, `gosh-server`, `GOSH_*` (NAME-1); installing also
  links `mosh-client` and `mosh-server` where not already present (CMD-4);
- speed: not more than 10% lower overall than pf.7 (SPEED-1), ahead of readability (SHAPE-5);
  templates in place of virtual calls where a profile points to them;
- requirement IDs are written in gosh's code and tests;
- the agent's review of each part is accepted; a third party reviews all of it before a class
  uses gosh (*Scope decisions*, Review). Who that is: not yet answered;
- nothing is offered to upstream.

## What was measured or seen, and has not been repeated here

- Speed of pf.7 on this machine, `../mosh-dev/tools/speed.py`, 2026-11-13: by the relay 13.1 and
  13.4 Mbit/s; by UDP 16.7 and 23.1, too unsteady to hold anything to 10% yet.
  *Check:* run it (it needs `../mosh-dev/.build`; `../mosh-dev/tools/build.sh` makes that).
- pf.7's release was published, and CI on its tag failed in the NAT lab's two-machines-in-one-room
  case, which has failed in three of the last five runs. Not known whether the test races or a
  session can stay on the meetup when it need not. *Check:* `../mosh-dev/docs/FINDINGS.md`, first
  entry; this repository's queue item 8.
- A test that a session returns to UDP and lets the ssh stream go (`relay.test`, `returned`) was
  written and seen to fail two ways. It is commit `6c0cc61` on branch `refactor/relay` of
  `../mosh-dev/mosh`. *Check:* `git -C ../mosh-dev/mosh log --oneline -2 refactor/relay`. Queue
  item 4 brings it here.
- `mobile-shell/mosh` pull request 1442 (another author) makes the fork's IPv6 fix. *Check:*
  `gh pr view 1442 --repo mobile-shell/mosh`.

## Left unfinished, in the old workspace

As this note was written; look before believing it (`git -C ../mosh-dev status -sb`).

- `../mosh-dev` had uncommitted changes that make v6 its requirements in force, and four commits not
  pushed.
- Branch `refactor/relay` of the fork was not pushed, and `../mosh-dev`'s `mosh` pointer names its
  commit.
- The owner's private meetup on this machine still runs under the old names, with pf.6.

## What the last session did not know

- It read viki-agent's README and nothing else of viki. What the librarian costs to run, and
  whether its outline suits C++ source, are unknown.
- It did not open `stmclient.cc`, `mosh-client.cc`, `mosh-server.cc` or the forwarding code. It read
  `relay.cc`, `relay.h` and `headless-smoke.py` in full. One thing found there: the 3 and 10
  seconds of PATH-1 are not in `relay.cc`, which uses 2.5 seconds throughout; they must be in the
  client.
- `./se --doctor` reports that the guide names a check command. It does not (queue item 14).

## Rules that bite on this machine

- Never stop a process by name: this is the owner's live mosh host (`CONTRIBUTING.md`).
- Do not commit, push or tag unless the owner asks.
