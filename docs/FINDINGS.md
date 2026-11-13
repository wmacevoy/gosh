# Findings

What we learned the expensive way: surprises, dead ends, measurements, the attack that worked. The
only document reality writes ([SE1/W10](../vendor/se1/guide/08-work.md#w10)). Blameless: say what
would have found it, not who should have asked ([SE1/H12](../vendor/se1/guide/09-people.md#h12)).

What was learned building the fork, up to 2026-11-13, is in `../mosh-dev/docs/FINDINGS.md`, and is
worth reading before migrating the part it concerns.

## 2026-11-13 — The design was accepted with claims its author had searched for and not read

- **What happened:** The agent wrote design v1 after reading the fork's changes to upstream's files
  and the added headers, and only searching the added `.cc` files. It said so in the design. The
  owner accepted it, and it was pushed. A review the same day, nine readers each given one part's
  code and not the author's reasoning, with a second reader trying to refute each finding, returned
  58 findings that survived. Among them: the clock claim was wrong for three of six files (the
  search had counted any clock call as the core's `timestamp()`; relay and copy have clocks of
  their own); the map's arrow from the command to the programs ran backwards; the header that
  defines the seam's interface was in no list; the patches called independent share lines; a test
  the queue would run "against pf.7's programs" stops every process under the directory it is
  given, which on this machine is the owner's live sessions; and `REQ1/CMD-3` is not met by the
  carried code for a plain forward to a stock server.
- **Kind:** K3. "Searched, not read" was written down as a limit and then built on as if it were
  not one, by the agent and, through the summary, by the owner.
- **Cost:** a design accepted and pushed, then rewritten within hours; no code was built on it.
  The review cost 73 agent runs.
- **Findable by:** reading the code before the acceptance was asked for, or asking for it with
  "the parts' `.cc` files have not been read" as the first line and not the last.
- **Changed:** the design is corrected in place and says so in its status; `CONTRIBUTING.md` has a
  rule against running tests on installed programs; the CI workflow no longer satisfies the
  doctor's check by a comment. For later parts the queue's "landed" already asks for a review by a
  session given the code and not the author's reasoning; this is the first evidence that it finds
  things.
