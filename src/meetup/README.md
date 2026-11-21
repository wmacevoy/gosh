# meetup

A machine with a public address, typically the student's own server, where the two ends of a
session that cannot reach each other learn where they appear to be, and through which their
datagrams are passed when they find no way to each other (`REQ15/PATH-4`, `REQ18/PATH-5`; design
v2, 2.2; `docs/PROPOSAL_meetup-registration.md`, revision 10). ssh is its only authority: a
session uses it with a ticket that whoever can log in to its machine is given.

```
gosh-server meetup [PORT]      answers STUN (port 3478 if none), and passes datagrams for the two ends of a ticket
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
| `messages.h` | REGISTER, REGISTERED, PASS, PASSED as bytes, each at its exact length; sealing and opening. Each sealed kind is opened by `Sealed<T, MeetupSealer>`, T the proved class that reads its text (`Strong::Meetup::Register`, `Registered`, `Pass`, `Time`), and read where the cipher left it. Logic | `messages-test.cc` |
| `sealed.h` | `Sealed<T, Creds>` and `Unsealed<T>`: sealed text, and what it opened to, owned where the cipher left it and never copied; only a `Sealed` makes an `Unsealed`, and a T reads it in place as an `Opened<T>`. Not proved: it calls the cipher | `messages-test.cc` |
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

**IPv4 and IPv6** (`REQ17/PATH-8`): the meetup listens on both, each where the machine has it,
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
`REQ18/PATH-5` by a model of what the meetup's own answers have shown: answered only where a
REGISTER was sealed and came from; passed only from where an end registered, only to the other end
of its ticket where that registered; a sealed message acted on once at most, and an end's counter
taken once at most, even after the meetup has let the ticket go ("passed on no second time"; since
2026-11-17, `REQ18/PATH-5`); nothing for a
ticket that has run out (ticket 1 runs out within reach); never more than `MAX_TICKETS` held (kind 4
registers 256 more). `make check` replays the corpus and 200 seeded changes of each input,
`tools/asan-replay.sh` under AddressSanitizer, and CI runs it under libFuzzer (`tools/fuzz.sh`).
Seen red with a PASS taken from anywhere (`registrations.h`); and, since the review of item 21
(2026-11-16), with each of four guards broken, every one of which the fuzzer passed before: the
PASS counter, the REGISTER counter (the reviewer's `register-replay`), the expiry check (`expired`),
and `make_room` (`full-table`).

**Closed 2026-11-17 (`REQ18/PATH-5`), found by that fuzzer:** a ticket the meetup let go, after two
minutes with neither end registering, or put out to make room, took its counters with it, and a
REGISTER recorded earlier, sent again from the address sealed in it, was taken again, and after it
a recorded PASS. Now, in version 2 of the messages:

- **REGISTER carries its time by the meetup's clock**, and one more than `CLOCK_SLACK` (60 s) from
  it is not taken. An end is let go only `END_IDLE` (120 s, twice the slack) after its last REGISTER
  was taken, so every REGISTER it was taken with is by then too old.
- **TIME:** a REGISTER sealed under the ticket's key, from where it says, whose time is too far off,
  is answered with the meetup's time and the counter of the REGISTER it answers. The end takes it
  only as the answer to its latest REGISTER, sets its reckoning of the meetup's clock, and registers
  again at once. So an end needs no right clock of its own: the meetup's is the only one that counts
  (the owner, 2026-11-17: "the refusal contains the server's roughtime").
- **The first REGISTER of an end held afresh is the lowest counter taken from it**, so no PASS
  sealed before it is passed.
- **A full table takes no new ticket** until one goes quiet, rather than put out one that may still
  be replayed (until then, the one that had gone longest without passing a datagram went).
- **What the meetup seals is numbered from its start time** (ms, times 1024) across every ticket.
  Until then each ticket taken counted from 0 again, so a ticket let go and taken again had
  REGISTERED sealed under one key with a nonce used before (found while closing the above).

**Not closed:** a meetup just started remembers nothing, so for its first minute it takes again a
REGISTER copied in the minute before it started, and after it the PASSes copied with it. Closing
it would need the meetup to write down when it was last running and refuse REGISTERs for a minute
after a start that follows soon after one (a minute's outage after each quick restart, a file kept,
and two meetups sharing a home making each other wait). Left open by the owner, 2026-11-17: "leave
it open". What it needs: the end's datagrams seen in the minute before a restart the attacker
cannot cause, and its address to send from; what it gains: sealed datagrams passed a second time.
`registrations-test` shows it, so that closing it shows too.

Seen red: `registrations-test` with each of the four guards broken; `finding-test` with an end
that ignores TIME, or believes one that answers no REGISTER of its own; the fuzzer with the time
check broken (its input is `registrations-fuzzer-corpus/replay-after-let-go`).
