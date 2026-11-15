# meetup

A machine with a public address, typically the student's own server, where the two ends of a
session that cannot reach each other learn where they appear to be, and through which their
datagrams are passed when they find no way to each other (`REQ15/PATH-4`, `REQ15/PATH-5`; design
v2, 2.2; `docs/PROPOSAL_meetup-registration.md`, revision 10). ssh is its only authority: a
session uses it with a ticket that whoever can log in to its machine is given.

```
gosh-server meetup PORT        answers STUN, and passes datagrams for the two ends of a ticket
gosh-server meetup-ticket      prints a ticket, good for 30 days: "gosh-ticket ID KEY"
gosh-server meetup-key --new   a new master key: every ticket made under the old one is no good
                               once the meetup is started again
```

The master key is `meetup.key` in gosh's directory (`~/.config/gosh/` on Linux), its owner's
alone; it is made the first time either of the first two runs.

It calls nothing of gosh's other parts. Path-finding calls its `ticket.h` and `messages.h`, as
one end of a ticket.

| File | What it holds | Test |
| --- | --- | --- |
| `ticket.h` | a ticket: its id, its key from the master key, when it stops being good, its text. Logic | `ticket-test.cc`, with the real cipher |
| `messages.h` | REGISTER, REGISTERED, PASS, PASSED as bytes, each at its exact length; sealing and opening. Logic | `messages-test.cc` |
| `registrations.h` | what the meetup does with REGISTER and PASS: who is where, which counters are used, what is passed to whom. Logic | `registrations-test.cc`: the fork's replay turned round |
| `answer.h` | the STUN question answered, no more than 200 a second. Logic | `answer-test.cc` |
| `master.*` | the master key's file, and gosh's directory. An edge | `tests/meetup-tickets.py` |
| `serve.cc`, `meetup.h` | the process: one UDP socket and the clock. An edge | `tests/meetup-tickets.py`, from outside, sealing with Python's AES-OCB |

## What was wrong, and what is not closed

The fork's meetup took a proof that anyone who saw one datagram could copy, and treated whoever
last named a pair as one of its ends (found 2026-11-13; shown with the real cipher 2026-11-15).
Now an end registers by sealing, under its ticket's key, the address it believes it is at, and
is taken only from there and only with a counter it has not used; a PASS is taken only from where
its end registered. `registrations-test.cc` shows each thing the onlooker could do refused.

Not closed (the proposal, section 6): a ticket lasts 30 days, whoever holds it; the meetup sees
which two addresses talk; a ticket's id and an end's counter are in the clear; IPv4 only.

## Not tested

- Datagrams through the meetup between two relays on different machines: on one machine the
  relays find a way straight to each other first. `src/pathfinding/punch-test.cc` passes them
  through the meetup's logic, and `tests/relay-meetup.py` shows both relays registering.
- If Python's `cryptography` package (AESOCB3) is missing, that test skips the exchange and says so.
