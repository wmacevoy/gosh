# strong

What is proved (`REQ6/SHAPE-5`). Every class and function here is in the namespace `Strong`, and
of each it is shown, from this text as it stands and for every input: it reads and writes nothing
outside its own memory, it ends, and it keeps what its `invariant()` says. The proving is another
project's, strong-domain-driven-design, which reads these files as they are committed; nothing
here knows of it. **Until a branch `strong/...` is gosh's main, what is on main is not what was
proved.**

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

| File | What it is | Used by |
| --- | --- | --- |
| `caller.h` | who called a `_checked` way in, and the end of the process when what it tests is not so. The one file here that is not proved | `bounded.h` |
| `bounded.h` | `bounded<a, b, type>`: a number from a to b, both included, as a type; making one expects that of the caller. `bounded_checked<a, b, type>`: made from any number, it tests, and the process ends if it is not within the bounds. Each proved once, for every type and bounds | `meetupcounter.h` |
| `meetupcounter.h` | `MeetupCounter`, a counter of 62 bits, and `MeetupCounterChecked`: names for the two above at 0 to 2^62 - 1 | `counterwindow.h`, `meetup/messages.h` |
| `counterwindow.h` | `CounterWindow`: which of an end's last 1024 counters are used. `take` expects a `MeetupCounter`; `take_checked` takes a checked one. Proved: memory, ending, and that the highest counter kept is one. Not proved: that no counter is taken twice | `meetup/registrations.h` |
| `stunanswer.h` | `StunAnswer`: a STUN answer read, from bytes sent by anyone. Every method is a way in (`parse_checked`, `kept_..._checked`): its one user is not proved | `pathfinding/stun.h` |
| `stunrequest.h` | `is_stun_request_checked`: whether bytes are a STUN Binding Request | `meetup/answer.h` and its users |
| `subarray.h` | `subarray<iterator>( a, n )`: the n elements from a, read by `at( i )`. What making one expects (the elements are there, and stay) is held before the program runs, by the lint of strong-domain-driven-design. Not proved: it keeps a pointer | `datagram.h` |
| `datagram.h` | `Datagram`: a `subarray` of bytes, as they were received | `stunanswer.h`, `stunrequest.h`, `meetupheader.h` and their users |
| `meetupheader.h` | `MeetupHeader`: what a datagram at the meetup's door is taken for: a kind, an end, a length, and where what is sealed begins, as numbers | `meetup/messages.h` |
| `meetupregister.h` | `MeetupRegister`: what a REGISTER says once opened: the family of where the end believes it is, 4 or 6, and the time by the meetup's clock as the end reckons it. Proved: it reads only a text of 27 bytes whose first is 4 or 6, and the time is its last eight, most significant first | `meetup/messages.h` |
| `meetupnonce.h` | `MeetupNonce`: what a meetup message is sealed with: who sealed it, for which end, and a counter of 62 bits. Proved to lose nothing either way, so no two senders under one key make one nonce. Not proved: that a sender's counter is never used twice | `meetup/messages.h` |

Tests: each is tested through the part that uses it (`registrations-test.cc`, `stun-test.cc`,
`answer-test.cc`) and its fuzzer; there is no test here, and no Makefile: these are headers.
