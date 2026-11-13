# Findings

What we learned the expensive way: surprises, dead ends, measurements, the attack that worked. The
only document reality writes ([SE1/W10](../vendor/se1/guide/08-work.md#w10)). Blameless: say what
would have found it, not who should have asked ([SE1/H12](../vendor/se1/guide/09-people.md#h12)).

What was learned building the fork, up to 2026-11-13, is in `../mosh-dev/docs/FINDINGS.md`, and is
worth reading before migrating the part it concerns.

## 2026-11-13 — A meetup's proof of its key can be replayed by anyone who sees one

- **What happened:** Reading `punch.cc` at pf.7 for the domain refactor proposal, a second agent
  found that the twelve bytes that show a meetup's key are four random bytes and a tag made from
  those four bytes alone (`proof_mark`, `meetup_proof_ok`). Nothing ties them to a time, a sender
  or a request, they travel unsealed at the front of every datagram a meetup is asked to pass on,
  and the meetup remembers good ones. So whoever can see one such datagram can use the meetup
  from then on, without the key. The first agent read the same lines and agrees. **Read, not
  demonstrated**: no one has replayed one. It cannot read or alter a session, which is sealed by
  its ends.
- **Kind:** K3. `REQ1/PATH-5` ("passes nothing on, for anyone who does not show the key") was
  taken as met because a test shows a stranger with no proof is refused.
- **Cost:** none yet; found before a class uses it.
- **Findable by:** asking, of the proof, what an observer of one datagram can then do
  ([SE1/T8](../vendor/se1/guide/06-testing.md#t8): demonstrate the attack first).
- **Changed:** nothing in behaviour: features are frozen and the fork is frozen. It is written
  here for the third party's review, and the proposal lists it for the owner (its question 9).
  When meetup lands (queue item 8), its test should replay a seen proof and say what happens.

## 2026-11-13 — Other things the same reading found, not checked by a second reader

- **What happened:** From `docs/PROPOSAL_domain-refactor.md`, section 1.3, each read by one agent
  and not run or re-read: the receiving end of a copy checks the names it is given but opens and
  renames through whatever is already at the destination, a symbolic link included (bears on
  `REQ1/CP-6`); the two ends of a session are each given the list of forwards and nothing checks
  that the lists agree; nobody notices if the local relay dies; `--help` of the built-in command
  prints leftover Perl; `--client=PATH` is accepted and ignored.
- **Kind:** K1: each is a question with a known place to look.
- **Cost:** none yet.
- **Findable by:** reading each part's code against its requirement before the part lands, which
  the queue's review of each part is for.
- **Changed:** nothing yet. Each is to be reproduced as a failing test when its part lands (copy:
  item 9; forwarding: items 5 and 6; relay: item 7; the command: item 10), or struck from here.

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
