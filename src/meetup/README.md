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
which two addresses talk; a ticket's id and an end's counter are in the clear.

**IPv4 and IPv6** (`REQ16/PATH-8`): the meetup listens on both, each where the machine has it,
answers STUN by both, and a ticket's two ends may be of different families. Tested by
`registrations-test.cc` (an IPv6 end and an IPv4 one), `tests/meetup-tickets.py` (the exchange
over `::1`) and `tests/relay-meetup.py ::1`.

**It answers from the address it was asked at** (2026-11-16, found by review). Its sockets are
bound to every address, and an end takes the meetup's datagrams only from the address it knows the
meetup by; a machine with several (common with IPv6) would otherwise answer from whichever the
system picked, and no end would ever register. `serve.cc` reads where each datagram came to
(`IP_PKTINFO`, `IPV6_PKTINFO`) and sends its answer from there; a datagram passed on goes from the
address its receiver last sent to (kept only for ends whose message was taken, at most
4 × `MAX_TICKETS`). Tested by `tests/relay-meetup.py 127.0.0.1 127.0.0.2`: the meetup named by
another of the machine's addresses; red before. The passing-on half is read, not driven: on one
machine the relays find a way straight to each other first.

A meetup's name is looked up and its first address used (`src/relay/local.cc`): a meetup with
both families whose IPv6 does not work is not tried by IPv4. Name it by its IPv4 address then.

## Not tested

- Datagrams through the meetup between two relays on different machines: on one machine the
  relays find a way straight to each other first. `src/pathfinding/punch-test.cc` passes them
  through the meetup's logic, and `tests/relay-meetup.py` shows both relays registering.
- If Python's `cryptography` package (AESOCB3) is missing, that test skips the exchange and says so.

**Fuzzed** (queue item 21): `registrations-fuzzer.cc`, new (the fork's meetup was fuzzed by
nothing), with `registrations-fuzzer-corpus/`. Anything from anyone, messages sealed under two
tickets by either end from anywhere with any counter, messages sent again, STUN requests; held to
`REQ15/PATH-5` by a model of what the meetup's own answers have shown: answered only where a
REGISTER was sealed and came from; passed only from where an end registered, only to the other end
of its ticket where that registered; a sealed message acted on once at most, and an end's counter
taken once at most, while the meetup holds the ticket ("passed on no second time"); nothing for a
ticket that has run out (ticket 1 runs out within reach); never more than `MAX_TICKETS` held (kind 4
registers 256 more). `make check` replays the corpus and 200 seeded changes of each input,
`tools/asan-replay.sh` under AddressSanitizer, and CI runs it under libFuzzer (`tools/fuzz.sh`).
Seen red with a PASS taken from anywhere (`registrations.h`); and, since the review of item 21
(2026-11-16), with each of four guards broken, every one of which the fuzzer passed before: the
PASS counter, the REGISTER counter (the reviewer's `register-replay`), the expiry check (`expired`),
and `make_room` (`full-table`).

**Not closed, found by that fuzzer** (the owner's to decide): a ticket the meetup has let go, after
two minutes with neither end registering, or put out to make room, takes its counters with it. A
REGISTER recorded earlier and sent again from the address sealed in it is then taken again, and
after it a recorded PASS. Shown: the fuzzer's record of "once", kept across the meetup letting the
ticket go, aborts on a REGISTER, two minutes and a quarter of quiet, and the same REGISTER again.
It needs someone who saw the end's datagrams and can send from its address; what they gain is a
replayed sealed datagram, which the session refuses as one it has had. `REQ15/PATH-5` says "passed
on no second time" without that limit.

It does not check "never with more bytes than it was sent" (requirements, threat model): a STUN
request of 20 bytes gets an answer of 32 (IPv4) or 44 (IPv6), so that sentence is untrue today.
