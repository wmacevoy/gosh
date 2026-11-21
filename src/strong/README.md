# strong

What is proved (`REQ6/SHAPE-5`). Every class and function here is in the namespace `Strong`, or one inside it named for its part, and
of each it is shown, from this text as it stands and for every input: it reads and writes nothing
outside its own memory, it ends, and it keeps what its `invariant()` says. Each proof stands beside
its class (`MeetupRegister.from`, `.claims.lean`, `.proof.cpp`: CONTRIBUTING.md says what each is),
the prover is `tools/prover/`, and `tools/proofs.sh` proves them all; the whole check runs it.
(Until 2026-11-21 the proofs were the project strong-domain-driven-design's.)

Branches, a staging workflow (the owner, 2026-11-20: "strong/* to strengthen, strong/main to stage,
main to version and merge outside of the strong workflow"): a change to what is proved is made on a
branch `strong/...`; it is merged into `strong/main` once proved and once gosh's check passes by
both compilers; `strong/main` is merged into `main`, which is versioned and takes every change
that is not of the strong work. **What is on main is what was proved only as of its last merge
from `strong/main`.**

Three rules, and the proving refuses gosh if one is broken:

1. **Everything here is proved.** A file under `strong/` holds proved classes and functions, and
   names for types and numbers; nothing else with a body, but `caller.h`.
2. **What is proved tests nothing it could prove.** A method may say what it expects of its caller,
   `pre( condition )`; that is proved at each call from another proved class, and nothing tests it
   when the program runs (g++ 16 does, in gosh's check: `tools/gcc16.sh`). A proved method takes,
   makes and calls nothing named `_checked`.
3. **Everything else in gosh comes in by a name that ends `_checked`, and by no other.** Even
   where nothing can fail: `StunAnswer::parse_checked` reads anything and answers. So every place
   that is not proved touches what is, is found by that word. The core (mosh), ssh, and every
   other part of gosh are not proved. They may make a `_checked` value, and call a `_checked` method: each tests what it is given when
   the program runs, and **the process ends** if it is not so, saying who called (`caller.h`). It is
   not an answer and not an exception. So they hand over only what they have made right
   themselves; they make no bounded number, and call nothing that says `pre`.

Laid out by part (the owner, 2026-11-20: "i was expecting to see strong/forwarding/..."): here, what
every part is written with; in a directory named for a part of gosh, that part's proved code, in a namespace of the same name (`Strong::Meetup::Header`).

**Shared**

| File | What it is | Used by |
| --- | --- | --- |
| `caller.h` | who called a `_checked` way in, and the end of the process when what it tests is not so. The one file here that is not proved | `bounded.h` |
| `bounded.h` | `bounded<a, b, type>`: a number from a to b, both included, as a type; making one expects that of the caller. `bounded_checked<a, b, type>`: made from any number, it tests, and the process ends if it is not within the bounds. Each proved once, for every type and bounds | `meetup/counter.h` |
| `subarray.h` | `subarray<iterator>( a, n )`: the n elements from a, read by `at( i )`; `subarray<iterator, of>` says by its type what they are, to the compiler only (the proofs read every window alike, so what is proved of a reader is proved whatever its window is said to hold). What making one expects (the elements are there, and stay) is held before the program runs, by the lint (`tools/prover/lint.py`). `fixed<n, of>( a )`: the n bytes from a, a window whose length is its type; a proved class reads one only at an index written out below n (the prover refuses any other), so where its reads fall is nothing to prove. Not proved: each keeps a pointer | `datagram.h`, `meetup/register.h`, `meetup/registered.h`, `meetup/time.h`, `meetup/pass.h`; a `fixed` is made by `src/meetup/sealed.h`, where the length has just been found n |
| `datagram.h` | `Datagram`: a `subarray` of bytes, as they were received | `pathfinding/stunanswer.h`, `pathfinding/stunrequest.h`, `meetup/header.h` and their users |
| `opened.h` | `Opened<T>`: a `subarray` of bytes, as the cipher gave them back, opened, and the text of a T, the proved class that reads it: the compiler refuses a `Datagram`, or another kind's text, where `Opened<Meetup::Register>` is wanted. It says nothing of who sealed them. For a text of any length; a text of one length is read as a `fixed` | made by `src/meetup/sealed.h`; read by `meetup/pass.h` |

**`meetup/`**: the meetup's door

| File | What it is | Used by |
| --- | --- | --- |
| `meetup/header.h` | `Meetup::Header`: what a datagram at the meetup's door is taken for: a kind, an end, a length, and where what is sealed begins, as numbers | `src/meetup/messages.h` |
| `meetup/register.h` | `Meetup::Register`: what a REGISTER says once opened: the family of where the end believes it is, 4 or 6, and the time by the meetup's clock as the end reckons it. It reads a `fixed<27, Register>`. Proved: memory, ending, and the family kept is 0, 4 or 6. Not proved: that the time is its last eight bytes, most significant first | `src/meetup/messages.h` |
| `meetup/registered.h` | `Meetup::Registered`: what a REGISTERED says once opened: the family of where the meetup saw the end, 4 or 6; the end compares where's bytes in place. It reads a `fixed<19, Registered>`. Proved: memory, ending, and the family kept is 0, 4 or 6 | `src/meetup/messages.h` |
| `meetup/pass.h` | `Meetup::Pass`: what a PASS says once opened: a datagram of 1 to 1,500 bytes, whose length it keeps; the meetup sends the bytes on from where they lie, copied once into the PASSED. It reads an `Opened<Pass>`. Proved: memory, ending, and the length kept is at most 1,500, and the text's | `src/meetup/messages.h` |
| `meetup/time.h` | `Meetup::Time`: what a TIME says once opened: the counter of the REGISTER it answers and the meetup's time. It reads a `fixed<16, Time>`. Proved: memory, ending, and the counter kept is under 2^62 (one that is not answers no REGISTER, and is not read). Not proved: that the numbers are the text's, most significant first | `src/meetup/messages.h` |
| `meetup/nonce.h` | `Meetup::Nonce`: what a meetup message is sealed with: who sealed it, for which end, and a counter of 62 bits. Proved to lose nothing either way, so no two senders under one key make one nonce. Not proved: that a sender's counter is never used twice | `src/meetup/messages.h` |
| `meetup/counter.h` | `Meetup::Counter`, a counter of 62 bits, and `Meetup::CounterChecked`: names for the two above at 0 to 2^62 - 1 | `meetup/counterwindow.h`, `src/meetup/messages.h` |
| `meetup/counterwindow.h` | `Meetup::CounterWindow`: which of an end's last 1024 counters are used. `take` expects a `Counter`; `take_checked` takes a checked one. Proved: memory, ending, and that the highest counter kept is one. Not proved: that no counter is taken twice | `src/meetup/registrations.h` |

**`pathfinding/`**: STUN

| File | What it is | Used by |
| --- | --- | --- |
| `pathfinding/stunanswer.h` | `Pathfinding::StunAnswer`: a STUN answer read, from bytes sent by anyone. Every method is a way in (`parse_checked`, `kept_..._checked`): its one user is not proved | `src/pathfinding/stun.h` |
| `pathfinding/stunrequest.h` | `Pathfinding::is_stun_request_checked`: whether bytes are a STUN Binding Request | `src/meetup/answer.h` and its users |

**`forwarding/`**: nothing yet.
`answer-test.cc`) and its fuzzer; there is no test here, and no Makefile: these are headers.
