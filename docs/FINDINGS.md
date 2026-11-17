# Findings

What we learned the expensive way: surprises, dead ends, measurements, the attack that worked. The
only document reality writes ([SE1/W10](../vendor/se1/guide/08-work.md#w10)). Blameless: say what
would have found it, not who should have asked ([SE1/H12](../vendor/se1/guide/09-people.md#h12)).

What was learned building the fork, up to 2026-11-13, is in `../mosh-dev/docs/FINDINGS.md`, and is
worth reading before migrating the part it concerns.

## 2026-11-17 — The owner's trial: VS Code's remote ssh through a gosh forward kept going

- **What was done:** the trial queue item 23 waited on (`REQ19/FWD-6`). The owner, on a Mac
  (gosh v0.2 from the release), ran `gosh -N -L 2222:localhost:22` to this machine, pointed VS
  Code's Remote - SSH (0.128.0) at `localhost:2222` (`HostKeyAlias` to the host's name), and kept a
  `while true; do date; sleep 1; done` running in its terminal.
- **What happened:** Wi-Fi off for 30 s, a roam to a phone's hotspot, and the lid closed for 5
  minutes: the window stayed connected, the loop never stopped. The lid closed for about 18 minutes
  (05:47 to 06:05 by the Mac): the window reconnected, by itself, in about a second. The loop kept
  going (the same shell); the gosh session and its `gosh -N` never stopped.
- **Why it reconnected, by the logs** (the Mac's Remote - SSH log; this host's processes): at wake,
  VS Code's own helper gave up its tunnel (`Server delay-shutdown request failed: socket hang up`,
  `local-server-8> Timed out`) and ran ssh again; the new ssh logged in through gosh's forward in
  0.8 s and found the same VS Code server running. No ssh message of a connection cut under it was
  seen (`Connection reset`, `closed by remote host`, `Broken pipe`), but the old ssh's last lines
  were not read, so that gosh's forward did not end it is inferred, not shown. sshd here has no
  `ClientAliveInterval`. In the same second gosh said "no answer by UDP; trying through ssh as well"
  and started a relay: VS Code's timer fired while gosh was regaining contact.
- **Open, seen in passing, not looked into:** whether the session goes back from the relay to UDP
  once UDP answers again (`REQ1/SHAPE-2`); and a `gosh-server relay` on this host, started 13:40
  UTC, whose ssh has gone (its parent is init), left running: not this session's to stop.
- **Kind:** K1, answered: the trial was the one question `-W` waited on.
- **What it changed:** queue item 23's block is the trial no longer; `-W` would not change VS Code's
  own reconnect at wake, and is waiting on the owner's word to build.

## 2026-11-17 — The meetup's replay after letting a ticket go, closed; and a nonce it reused

- **What happened (1):** the replay found on 2026-11-16 (below) was closed by the owner's choice, the
  meetup's clock, with the refusal carrying the meetup's time (`REQ18/PATH-5`; `src/meetup/README.md`).
  The first thought was that the counter could carry the time, since it starts from the time of day;
  it cannot: it starts from when the relay started and only counts up, so a long session's REGISTER
  looks as old as the session. REGISTER now carries its time apart from the counter.
- **What happened (2):** while closing it: what the meetup sealed for a ticket (REGISTERED) was
  numbered by a counter kept with the ticket, from 0. A ticket let go and taken again started at 0
  again, under the same key, so REGISTERED went out twice with one nonce and different words.
  Under OCB that tells an onlooker how the two differ, and weakens the seal. Shown by
  `registrations-test` (red with the counter from 0); now one counter for everything the meetup
  seals, from its start time in milliseconds times 1024.
- **What happened (3):** the first thought for a restarted meetup (refuse every REGISTER for a
  minute after starting) would have meant a minute's outage on every start and a minute's wait in
  every test that starts one; left, and said in `REQ18/PATH-5`.
- **Kind:** (1) K1, then a K3 caught by reading before building ("the counter is a time"). (2) K3:
  the fuzzer's model reset its record when a ticket was let go, so it could not see a nonce used
  again either; the meetup's tests never let a ticket go and took it again.
- **Findable by:** (2) a test that lets a ticket go, takes it again, and compares what is sealed;
  now in `registrations-test`.
- **Changed:** messages version 2 (REGISTER's time, TIME); the meetup's guards and their tests,
  each seen red; the fuzzer holds "once" for good; requirements v18.

## 2026-11-16 — Reviews of the command, the extension and the fuzzers; what the stronger fuzzers found

- **What happened:** three sessions, each given only the code and the requirements, reviewed items
  10, 11 and 21. Among what they confirmed: a host or copy end beginning with `-` reached ssh as an
  option, in the command and in the extension (`-oProxyCommand=...` runs a command on the user's
  machine with OpenSSH before 9.6); the extension ran what the host downloaded without checking it;
  the installer replaced a user's own `mosh-server` in `~/.local/bin` when that was off its PATH;
  `--stun=none` did not keep a session off the meetup; and the fuzzers were weaker than their
  claims (the meetup's could not see a message acted on twice; the copy's saw one file only; the
  check's replay ran without AddressSanitizer). All mended, each seen red first.
- **And:** the meetup fuzzer, made to hold "no second time", found that the meetup itself does not
  keep it: once it lets a ticket go (`END_IDLE`, two quiet minutes; or to make room for another),
  a REGISTER recorded earlier is taken again, and a PASS recorded after it is passed on. The
  window is what the meetup remembers, and it forgets. Not mended: closing it needs the meetup to
  refuse an old counter for a ticket it has forgotten, which means either keeping something per
  ticket for its 30 days, or judging a counter's age by the clock (counters start from the time
  of day), with the clocks of the end and the meetup then in the requirement. A question for the
  owner: which, or accept the window and say so in `REQ15/PATH-5`.
- **Kind:** K3 in each case: the author's tests passed, and the author's fuzzers' properties were
  the author's.
- **Changed:** the fixes; every fuzzer replayed under AddressSanitizer in the check; the meetup's
  gap written in its README's "Not closed" and here.

## 2026-11-16 — The wire's last change: what running the fork's command test against gosh found

- **What happened:** with gosh's names in place, `wrapper.test` (until then run against pf.7's
  command only, because it called the command `mosh`) was given the names as arguments and run
  against gosh's. It found `gosh-server stun 60000`, the fork's dropped word, taken for "start a
  server bound to the address `stun`": it tried, failed to bind, and exited 0. The requirements
  drop the old names "with no aliases"; nothing said what a dropped word should do, and the fork
  refused it. Now it is refused and the new word named. And the usage text, under `gosh --help`,
  printed Perl (`my $copy_usage = qq{`), as design v2 had found and not yet mended.
- **And:** `stock-session.py` failed once in the check (gosh's client, stock server, "no terminal
  session") and passed six times alone and in the next whole check. Its failure said nothing of
  what the client drew, so whether the session never came up or the typed line was lost cannot be
  told. Not reproduced.
- **Kind:** K2, for the dropped word; an unexplained red, for the session.
- **Findable by:** running the fork's tests against gosh as well as against pf.7 wherever only a
  name stood in the way.
- **Changed:** the refusal; `wrapper.test` in the check against gosh's command; the stock-session
  test now says what was drawn and whether it typed, so the next failure can be read. If it fails
  again, it is quarantined and queued (SE1/T4), not rerun until green.

## 2026-11-16 — CI red for a day unseen; the relay ran a stock server; ~/.local/bin is not where ssh looks

- **What happened (1):** CI failed on every commit from `53075c3` (2026-11-14 23:20) to `9048b1a`:
  `bytes-test.cc` used `uint32_t` without `<cstdint>`, which this machine's GCC 12 brings in
  through `<string>` and Ubuntu 24.04's GCC 13 does not. The check stops at the first failing
  part, so the relay, path-finding, the meetup and IPv6 were never run by CI. Every commit was
  gated on the local check, which passed. Found when making v0.1.
- **What happened (2):** the command's remote relay and meetup ticket ran `mosh-server relay` and
  `mosh-server meetup-ticket` by name. On a host with gosh in `~/.local/bin` and a stock mosh in
  `/usr/bin`, where `REQ6/CMD-4` makes no `mosh-server` link, that is the stock server: the session
  came up by UDP but the ssh fallback never did. The tests gave the relay its command directly,
  so none went through the command's line. Shown from outside first (`tests/gosh-stock.py`, "a
  host with both, over ssh": red, then green); now `stock.h`'s `helper_command` runs gosh-server.
- **What happened (3), the trial queue item 10 asked for:** a home made from Debian 12's
  `/etc/skel`, and bash run as sshd runs a command (`bash -c`, `SSH_CLIENT` set, sshd's default
  `PATH`): `~/.local/bin` is not on the `PATH`, because `.bashrc` returns at once when not
  interactive and only `.profile`, for a login, adds it. *Simulated, not through a real sshd*:
  there is no sshd login and no container on this machine. gosh's own command adds `~/.local/bin`
  itself, so it is not affected. **A stock `mosh` is**: it asks for `mosh-server` by name, and
  `REQ6/CMD-4`'s link in `~/.local/bin` is not found. So CMD-4 as written gives a stock client
  nothing on such a host unless its user passes `--server=.local/bin/mosh-server`.
- **Kind:** (1) K3: "the check passed" was read as "CI passes". (2) K2. (3) K1, inferred on
  2026-11-13 and now shown, by simulation.
- **Findable by:** (1) looking at CI's result after each push; (2) a test that goes through the
  command's own line on a host with both servers; (3) the trial.
- **Changed:** (1) the include, and the habit: CI's result is read before a commit is reported.
  (2) the fix and its test. (3) a question for the owner on `REQ6/CMD-4`'s next version (the
  install could put the line in `~/.bashrc` above its guard, which edits a user's file; or the
  link could stay with the documented `--server`; or the requirement could go).

## 2026-11-15 — The meetup replaced by tickets; path-finding switched on; an oversized probe aborted the relay

- **What happened:** Path-finding was switched on with its meetup half replaced by tickets
  (`PROPOSAL_meetup-registration.md`, revision 10). The first agent's paper attack on the fork's
  `punch.cc` had said that a probe-marked datagram of any length up to 65,528 bytes went to the
  cipher's `decrypt`, whose 2,048-byte buffer is guarded by an `assert` alone. **Demonstrated:**
  `punch-test.cc` sends one of 4,008 bytes to a path-finding socket; without the new length check
  the test aborts in that assert (`crypto.cc:259`). In a relay that is anyone on the Internet
  who can reach its port ending it. Now a probe is opened only at its one length.
- **And:** the fork's replay of a meetup's proof (2026-11-13, below) is shown refused, turned
  round, in `registrations-test.cc`, and through a real meetup and a second implementation of the
  cipher (Python's AES-OCB) in `tests/meetup-tickets.py`. Six paper attacks on revisions 1 to 9
  were spent before the owner set them aside for the simplest design ("no new auth"); what they
  found about sign-in, keys and clocks is kept in the proposal's history.
- **Kind:** K3 made K0: a fault believed from reading, now seen; then fixed.
- **What changed:** `PROBE_LEN` in `punch.h`, with its test seen red; the meetup takes only
  tickets; `tools/check.sh` runs `punched` and `relay-meetup.py` against gosh.

## 2026-11-15 — Item 18's review said "not yet": the fix had made a connection with no other end, and then went on treating it as one

- **What happened:** the first fix for the reset after a finish marked the connection as having
  lost its other end and delivered what it held. A session given the commit then showed, with
  the test's own harness: a segment of the other end's that arrived after its reset threw out
  of the logic (the send buffer had been emptied under counters that still pointed into it);
  an old acknowledgment arriving late started the retransmission timer again, which 211 s
  later ended the connection with a reset and discarded the held bytes, the very loss the fix
  was for; an application still writing was no longer read from and hung for ever; and where
  the fix ended a connection with a reset "to tell the application its answer was lost", the
  reset at the application's socket threw away what it had not yet read (shown on loopback:
  65,536 of 2.5 MB read). It also showed the commonest way to meet all this: only the last
  acknowledgment lost.
- **What would have found it:** asking, of a new state, what every existing path does when it
  meets it; the tests only walked the one path that set it. Ten of sixteen deliberate breaks of
  the first fix failed no test.
- **What it changed:** from the reset on the connection has no other end, in one place each:
  nothing is sent, nothing late is looked at, no timer, further writes taken and dropped, and
  the end is always in order. Each finding was first a failing test (eight checks red). Eight
  breaks of the revised fix: all noticed, two after checks were added.
- **For the owner, not done:** a closed end that did not answer a bare acknowledgment with a
  reset would save an older peer's bytes in the commonest case. The reviewer showed it working
  with the old logic at both ends. Its cost: an end whose peer's abort was lost, and which only
  receives, is no longer told by its own window update and waits for its application. It
  changes what goes on the wire.

## 2026-11-15 — The second review of the relay: accepted, with what the fixes leave open

- **What happened:** a session given the security fixes, the dying-relay change and their
  claims tried to break each at the programs. Its verdict was to accept. It showed the first
  fix working (an answer from a less likely address no longer shuts out the server) and showed
  its limit (one from a **more** likely address still does); it read a race in the third (a
  port the user shares can be borrowed for an instant by another user); it found that a relay
  restart which itself fails was never tried again though the client had said "starting
  another"; and that undoing the first, third or fourth fix where the programs apply them
  fails no test.
- **What it changed:** the restart is tried again each second until it holds, and nothing is
  said of starting another while the session is ending. The limits are listed in
  `src/relay/README.md` under "What these fixes do not close". Not done: tests that drive the
  first, third and fourth fixes at the programs (the first is a race, the third needs a second
  user; the fourth could be done with a relay command that prints and exits, and is worth a
  queue item if the relay is to be relied on).

## 2026-11-15 — A relay that died was not noticed; now it is, and two limits are accepted

- **What happened:** the reading of 2026-11-13 said nobody notices if the local relay dies. The
  owner, 2026-11-15: "dying relay seems important, can you add tests." `tests/relay-dies.py`
  kills the relay of a session carried by nothing else and sends bytes through a forward:
  against pf.7, and against gosh as it was, they never come back and the client says nothing.
- **What it changed:** gosh's client looks at its relay each turn, says when it has stopped, and
  starts another, not within a second of the last (`src/relay/start.h`, with numbers in its
  test; the seam's hooks). The test kills two relays in a row and is in the check.
- **Accepted by the owner the same day, with why:** (1) the check that a new sender on the
  relay's loopback socket is a socket of the same user works on Linux only: "accept linux and
  write why. it is a denial attack since the stream is encrypted." Another user of the client's
  own machine can at most stop the session or see sealed datagrams. (2) "accept untested bsd":
  no BSD or macOS machine has run the relay, and the one line that is there for OpenBSD has
  never run. Both are said in `src/relay/README.md`. *(The agent's note: these read as scope
  decisions; they are in the README and here, and not in the requirements, which only a new
  version changes.)*

## 2026-11-14 — The carried relay: five things a reader should decide on before release

- **What happened:** the review of the relay (queue item 7; a session given the code and the
  fork's) was asked what an attacker on the path, a local user, or a hostile remote relay can
  make the relays do. All five are the fork's and unchanged; none reads or alters a session,
  whose datagrams stay sealed. **Read, not demonstrated** at the programs; two were shown
  against the rule in `paths.h` with numbers.
  1. *A local user on the server can take over the relayed path.* The remote relay sends each
     datagram to the address ssh reached, the address it was told, `127.0.0.1` and `::1`, and
     stays with whichever socket answers first. The server listens on one address, so the
     loopback ports are free for any local user, and the port is in the relay's arguments. One
     answer, and the relay closes the real socket for the life of that ssh connection.
  2. *Anything arriving at the local relay's direct socket counts as the server answering.* The
     source is not checked. Whoever can reach that port can keep the ssh way from ever being
     started (by the rule: 480 datagrams from the client in two minutes, none by the stream).
  3. *The local relay takes "the client" to be whoever last sent to its loopback port.* A local
     user on the client's machine can have the server's answers sent to themselves until the
     real client next sends, and can have datagrams passed on.
  4. *One byte from the stream resets the wait between attempts.* A login that prints something
     and then fails means an ssh login every second (by the rule: 600 in ten minutes, against
     10), which is what the five-minute cap was there to prevent.
  5. *A hostile remote relay* can do little while path-finding is off: feed datagrams the
     session refuses, and cause 4. With path-finding on (item 8) its words can point the local
     relay's probes at addresses of its choosing.
- **Kind:** K1 for the owner: each is a question of whether to fix before release, and a fix
  changes what the fork's relay does.
- **What it changed** (2026-11-15; the owner: "fix security first"): 1 to 4 are fixed in gosh's
  relay, which now differs from the fork's on purpose in four marked places
  (`src/relay/README.md`, `whose.h`). The second was first made to happen from outside:
  `tests/relay-spoof.py` sends a datagram of its own to the relay's socket five times a second,
  and against pf.7, and against gosh before the fix, a forward then carries nothing; against
  gosh now it carries on. The first, third and fourth are tested as rules with numbers and
  text; their lines at the edge are read and not driven (the first is a race to show; the third
  needs a second user). The third is only as good as the kernel's list of sockets, which only
  Linux gives: elsewhere gosh cannot tell and behaves as the fork did. The fifth cannot happen
  while path-finding is off, and is in queue item 8's *done*.

## 2026-11-14 — The relay's review: the logic matched the fork's, and the wiring did not

- **What happened:** the reviewer found no difference between the fork's relays and
  `relay.cc` with `paths.h`. It found four in how the client was wired to them, all the
  author's: (1) the line of the stream patch changed to let a client keep the right to start a
  process was the server's, not the client's, so on OpenBSD a client would have been killed when
  it started its relay; (2) `MOSH_RELAY=never` with a command still in the environment meant
  "when needed", and a unit test had pinned that as intended; (3) path-finding was switched off
  in the local relay only, so a pf.7 client could still have it run in gosh's remote relay,
  which the README said could not happen; (4) the server's address was taken as the last two
  words of the command line, where the core takes the first two that are not options.
- **What would have found them:** (1) reading the built file where the patch lands, not the
  patch; (2) reading the fork's `mosh-client.cc` as well as `stmclient.cc`: the rule was in two
  places and one was cited; (3) asking, of a switch, at which ends it is read; (4) a contract
  for what the command hands the client, in place of a comment.
- **What it changed:** all four fixed with tests where a test can reach (the mode's word, the
  operands; OpenBSD and the pf.7-client case are read only). Nine more checks for breaks the
  reviewer showed the unit tests missed. Also found, not fixed: `tests/headless-smoke.py`'s
  check that the relay goes when the client does cannot fail in the `translated` case (it looks
  for a process by the wrong address); it is the fork's test and is left for item 4's list.
  The relay is considered a little earlier in each pass of the client's loop than the fork
  considered it; in one pass in which the session also ends, a relay may be started that the
  fork would not have started, and it goes with the client.

## 2026-11-14 — A closed end resets a late segment, and the other end throws away bytes not yet taken

- **What happened:** the review of selective acknowledgment (a session given the code) ran
  connections with segments reordered and duplicated, which nothing in this repository does.
  It found a fault that is the fork's and older than the change under review.
- **What it is:** an end whose connection has closed in order forgets it at once. A late or
  duplicated segment for it is then answered with a reset; the other end believes the reset,
  ends its connection, and discards what its application had not yet taken.
- **What was seen** *(by the reviewer, in a harness of its own that is not in the repository;
  not run again here)*: with 30 in 100 segments reordered and a slow application at one end, 4
  of 100 connections ended this way with selective acknowledgment off, 11 of 100 with it on;
  in one, 167,212 of 284,228 bytes had been delivered.
- **Why no test here found it:** it needs reordering or duplication, and an application still
  reading when the other end closes. Loopback does neither.
- **What would have found it:** a unit test that carries segments out of order. `Pair` in
  `connection-test.cc` can lose segments now, and cannot yet reorder them.
- **What it changed** (queue item 18, taken up by the owner; 2026-11-15): made to happen first,
  as a unit test with numbers, and it is worse than the reviewer saw. It needs no reordering:
  a megabyte sent to a reader that has not yet taken it, both ends finished, and the reader's
  own word that its window has opened is what brings the reset. Up to 1 MB that had been
  acknowledged was thrown away. Now a reset that follows the other end's finish does not
  discard what is held: it is delivered, and then the connection ends. `Pair` in
  `connection-test.cc` can carry segments backwards and twice, and transfers under both arrive
  whole. With selective acknowledgment off, the logic still agrees with the fork's adapter over
  5,000 scenarios: real sockets are never slow, so that test never reached this. Not fixed: a
  pf.7 end as the slow reader (`src/forwarding/tcp/README.md`).

## 2026-11-14 — The review of selective acknowledgment: two faults that its author's tests passed

- **What happened:** the change landed with every test green, each seen red. A session given
  only the code then broke it two ways in ten minutes: a range said to be held that covered a
  FIN made the sender read past its buffer (an exception), and a sender that did not count held
  bytes against the receiver's window sent 50 KB past the room it was given.
- **What would have found them:** asking of each input what happens when it is wrong or lying
  (SE1/R6) for the range itself, not only for whether a range is true; and a test with a slow
  application and a loss together, where each had a test alone.
- **What it changed:** `84ce53e`: both fixed, a test for each, and two tests of what the
  receiver says and the sender reads. The reviewer's randomized runs (reordering, duplication,
  sequence numbers wrapping: about 3,700 connections, all whole) are not in the repository.

## 2026-11-14 — Selective acknowledgment: a third of the datagrams, and the inference held

- **What happened:** queue item 17 gave forwarding's TCP selective acknowledgment (commit
  `6465584`), on the inference in the entry below: that a burst overruns the receiving socket's
  buffer and the fork's repair then sends again most of what had arrived.
- **What was measured, on this machine, loopback:**
  - A 40 MB copy over a stream, counted by the kernel (`/proc/net/snmp`, every UDP datagram the
    machine sent meanwhile): pf.7 235,192, 238,343 and 238,961; gosh 78,273 to 83,517 over eight
    runs. About 72,000 is the least possible (the data and its acknowledgments). Two gosh runs
    taken directly after a pf.7 run read 114,896 and 126,936; taken to include pf.7's last
    datagrams, *(inferred, not checked)*.
  - The receive buffer still overflows as often (6,000 to 10,000 datagrams dropped a copy,
    either program). What changed is the cost of each drop: one segment sent again.
  - The speed gate, 30 runs of each, turn about: a TCP forward 47.8 Mbit/s against 27.9 for the
    fastest before (171%); a copy over a stream 38.7 against pf.7's 24.6 (158%). Processor time
    for each megabyte: 0.048 against 0.097, and 0.052 against pf.7's 0.147.
- **What the tests taught:** the first unit tests for it passed with the repair switched off,
  with a false claim believed, and with the held ranges not passed over: bytes sent were the same
  and only the number of exchanges, or a path no acknowledgment rescued, told them apart. Each
  was sharpened until the break was seen red (SE1/T3). A count of bytes alone does not test a
  repair; count the round trips too.
- **Not done:** nothing stops the overflow itself (the sender may still have 1 MB on its way to
  a 200 KB buffer). It no longer costs much, so it is left; pacing would be a new change.
- **What it changed:** `src/forwarding/connection.h` and its unit tests; `tools/fastest` names
  `6465584` for a TCP forward and for a stream; item 6 is no longer blocked.

## 2026-11-14 — A gain on a forward was a loss on a stream; and both programs send three times what they need

- **What happened:** Item 15 replaced a connection's string buffers with a byte queue, and the
  speed gate put a TCP forward at 157% of pf.7. The owner changed `REQ12/SPEED-1` to keep that gain.
  Item 6 then gave gosh streams, and a session given the code (not the author's account) timed
  them: a 40 MB `gocp` took gosh about 34 s and pf.7 about 5 s. Here, on a quiet machine, it was
  16 to 20 s against 9 to 15 s; it varies a great deal. The gate had passed, because it measures
  only a TCP forward.
- **What was found, by running:**
  - With the old buffer behaviour put back in a build copy, gosh's stream took about 13 s, level
    with pf.7. So the buffer change is the cause of the difference.
  - The kernel's counters (`/proc/net/snmp`, `RcvbufErrors`) over one copy: both pf.7 and gosh
    have 3% to 9% of their datagrams dropped because the receiving socket's buffer is full, and
    both send about 230,000 datagrams for a copy that needs about 70,000.
  - Having the core's two loops read every datagram that has arrived each pass, in place of
    one, did not help: drops stayed near 3% and the copy was no faster.
- **What is inferred, not measured:** the sender may have up to 1 MB on its way; the receiving
  socket's buffer (`rmem_default`, 212,992 bytes here, which is also the most a program without
  root may ask for) holds about 100 KB of datagrams; a burst overruns it; and the repair after
  several losses is to send again everything outstanding, most of which had arrived. The time
  the fork spent moving its buffer slowed its sender enough to soften this. That this is why the
  rate by UDP was always "unsteady" is likely and not shown.
- **Kind:** K3, twice. "157% of pf.7" was true of one kind of traffic and was recorded as the
  fastest version; and "loopback has no loss" was assumed by the measurement's own description
  ("with no delay and no loss, so what it shows is what the programs cost").
- **Cost:** a part accepted on a figure that did not hold for the next part; half a day.
- **Findable by:** measuring every kind of traffic the programs carry before recording a gain;
  and counting the datagrams a transfer sends against what it needs, which nobody had.
- **Changed:** the owner chose to fix the repair of losses and not to take the buffer change
  back ("Do 2. This will haunt in other ways until it is done."): requirements v14 takes up
  selective acknowledgment (queue item 17) and has `REQ14/SPEED-1`'s measurement include a copy over a
  stream. Item 6 waits on item 17. Until item 17 lands, gosh's streams are slower than pf.7's,
  and `tools/fastest`'s entry by UDP is the fastest for a TCP forward only.

## 2026-11-14 — A differential test that agreed over millions of segments, and compared too little

- **What happened:** `connection-diff` held the reshaped connection logic to the fork's adapter
  over 20,000 scenarios and 4.8 million segments with no difference, and caught 17 of 21
  deliberate breaks. A session given the code and not the author's account then broke the logic
  seven more ways that passed both tests, and showed why: a believed reset arrived about once in
  200 steps, so most connections were dead by step 170 of 600 and no end ever received more than
  26 KB; the 1 MB buffers and windows above 64 KB were never compared. The test also ignored
  when an application is told there is no more, and counted an application's write only if the
  socket took all of it, so it would fail on a machine with small socket buffers.
- **Kind:** K3. A large count of agreeing segments was taken for coverage.
- **Cost:** an hour to fix; nothing had been built on it but the buffer change, which the fixed
  test still passes.
- **Findable by:** asking what the scenarios reach, not how many there are: how long a
  connection lives, and how much it carries.
- **Changed:** four kinds of scenario by seed (most without resets, with few losses and heavy
  writing): 6.8 million segments over the same 20,000. It compares the end of delivery, and a
  short write is what was written. The unit test gained the cases a socket pair cannot make.
  Of 15 breaks rerun, 14 are caught; the one that is not needs more than a megabyte of
  out-of-order data held at once. Two are caught only by the deep run (`./connection-diff 20000
  1000`), which the check does not make: run it when the logic changes.

## 2026-11-14 — The one erase, taken out: a forward by UDP at about one and a half times pf.7

- **What happened:** The profile of pf.7 (below) put about 40% of a forward's processor time in
  one `memmove`: the sender moving what was left of its send buffer down on every acknowledgment.
  With forwarding's TCP reshaped into logic (`src/forwarding/connection.h`) and held to the fork's
  adapter by a differential test, the two buffers were made a byte queue (`Gosh::Bytes`) that marks
  its front as gone and moves what is left only now and then. Four lines of the logic changed.
- **What showed nothing else changed:** `connection-diff` still agrees with the fork's adapter,
  segment for segment, over 20,000 scenarios; the outside tests pass; a gosh end still works with
  a pf.7 end each way.
- **Measured:** the speed gate, by UDP, 30 alternating runs of each:

  | | pf.7 | gosh |
  | --- | --- | --- |
  | rate, median (lowest to highest), Mbit/s | 22.9 (14.5 to 44.8) | 35.8 (15.8 to 62.0) |
  | processor time, median, CPU s/MB | 0.1333 | 0.0930 |

  gosh's rate is 157% of pf.7's, and it spends about 30% less processor time per megabyte. One
  run of the gate before the change, same day, same code otherwise: 109% and 0.1335. Profiled as
  pf.7 was, the client's share of processor time went from 71% to 47%, level with the server's,
  and the largest single cost is now OpenSSL's sealing.
- **Kind:** K0 now: what the profile predicted was measured.
- **Cost:** an afternoon, almost all of it the reshaping and its tests; the change itself is small
  because the logic had been taken apart from the sockets first.
- **Findable by:** the profile. Nothing else pointed at it.
- **Changed:** `REQ6/SPEED-1` asks only that gosh not be more than 10% slower; nothing requires
  this. It is recorded because it moves what later parts are compared with: the gate compares gosh
  with pf.7, so a later part that lost half of this gain would still pass. If the owner wants a
  gain kept, that is a requirement to write.

## 2026-11-14 — The speed gate: what it took to hold a rate to 10%, and a pass it gave for nothing

- **What happened:** The owner kept the gate as the requirement has it ("not more than 10% slower
  overall compared to pf7"), so the measurement had to decide a 10% difference in a rate that
  varies. Three things were learned making it do that.
  1. *Fifteen runs are not enough by UDP.* pf.7 against itself, alternating, 15 runs of each: the
     "rate under test" was 90% of the reference's, on the line, with nothing changed. By the relay
     it was 99%.
  2. *Thirty are.* Thirty single runs of pf.7 by UDP ranged from 13.5 to 28.9 Mbit/s (median 17.0;
     the day before it was about 21, so the machine drifts and a stored baseline would not do).
     Resampling them, two medians of 15 differ by more than 10% about 3 times in 100 by chance, two
     medians of 30 about 3 times in 1,000. This assumes runs are independent. The gate now takes 30
     alternating runs of each by UDP and 15 by the relay, and takes about 15 minutes.
  3. *The first run of the gate on gosh passed a thing gosh cannot do.* It reported gosh by the
     relay at 139% of pf.7. gosh has no relay: its client ignored the setting and went by UDP, and
     the measurement had no way to tell. A run by the relay now counts only if the client said it
     was going through ssh; gosh's is refused and reported as not counted, and pf.7's still
     measures (each seen).
- **Kind:** K3, the third: a gate that says PASS with no way to say "that was not the relay".
- **Cost:** about an hour of measuring; nothing was accepted on the false pass.
- **Findable by:** asking of each figure what would make it wrong, and running a thing against
  itself before trusting a comparison.
- **Changed:** `tools/speed.py --against REF_SERVER REF_CLIENT SERVER CLIENT` is the gate, with
  the numbers above written into it. First verdict, gosh (queue item 5, the fork's forwarding
  carried unchanged) against pf.7 by UDP, 30 runs of each: **94%, pass** (medians 23.2 and 24.9
  Mbit/s). The relay is not counted until gosh has one (queue item 7). The gate is not in the
  check that runs on every push; it is run when a part lands.

## 2026-11-14 — Where pf.7's processor time goes: one erase, in the sender

- **What happened:** With `perf` installed (`tools/setup-root.sh`, run by the owner), pf.7 was
  profiled carrying 20 MB each way under `tools/speed.py` (`perf record -F 999`, then again with
  call graphs). Of all processor time in the run, 71% was the client (the sending end here), 25%
  the server. About 40% of the whole was one place: `memmove` in libc, called from
  `std::string::erase` in `AdapterPort::process_ack` (`tcpadapter.cc:657`,
  `c.send_buf.erase( 0, acked )`). The send buffer holds up to 1 MB (`SEND_BUFFER`,
  `tcpadapter.h:88`), and every acknowledgment moves all that is left of it down to the front.
  OpenSSL's sealing and opening was about 20%, both ends together. The programs' own code,
  including every virtual call, was about 12%.
- **Kind:** K2. Nobody had asked where the time went; design v2 inferred "copies, not virtual
  calls" from reading and could not name which.
- **Cost:** ten minutes, once there was a profiler. Under `perf` the rate fell by about half, so
  these are shares of processor time, not a speed figure.
- **Findable by:** a profile before choosing what to make fast, which SE1/O10 asks for and queue
  item 4 now does.
- **Changed:** nothing in pf.7, which is frozen, and nothing in queue item 5, which carries this
  code unchanged on purpose. For item 15 (forwarding's TCP reshaped): the send buffer need not be
  moved on every acknowledgment (an offset, compacted now and then, is the plain way), which would
  remove the largest single cost of a forward. `REQ6/SPEED-1` asks only for not slower; this says
  the reshaping has room, and that templates in place of virtual calls would be aimed at 12% while
  one line holds 40%. The same pattern is at `tcpadapter.cc:855` for the receive buffer, which
  did not show in this run.

## 2026-11-13 — The baseline for speed: pf.7 on this machine, and a steadier figure than Mbit/s

- **What happened:** `tools/speed.py`, brought here and given a second figure, measured the fork's
  programs at `mosh-1.4.0-pf.7` (built in `.build/pf7`) twice, 20 MB a run, 5 runs each way, on
  loopback, on the development machine (2 cores, AMD EPYC-Milan, load about 1):

  | way | Mbit/s, median (lowest to highest) | CPU s/MB, median (lowest to highest) |
  | --- | --- | --- |
  | udp, first | 21.5 (18.4 to 28.5) | 0.1380 (0.1255 to 0.1425) |
  | udp, second | 20.8 (17.4 to 26.3) | 0.1355 (0.1310 to 0.1410) |
  | relay, first | 13.7 (13.2 to 14.9) | 0.2415 (0.2280 to 0.2495) |
  | relay, second | 13.4 (12.8 to 14.7) | 0.2425 (0.2285 to 0.2530) |

  By UDP the rate's runs spread by half again (17 to 28) while the processor time per megabyte
  stays within about 7% of its median, and the two medians of each figure agree to 3%. And by UDP
  the programs used about 2.7 processor seconds over about 7.5 seconds of sending 20 MB: the
  processor is not what limits the rate. That fits design v2's inference that the unsteadiness is
  recovery from loss on loopback and not the cost of the code; it does not show it.
- **Kind:** K1 answered in part: the requirement's own note said the UDP figure was "too unsteady
  yet to hold anything to".
- **Cost:** three minutes of measuring.
- **Findable by:** measuring what the programs spend as well as what the clock saw.
- **Changed:** `REQ6/SPEED-1`'s kept measurement now reports both figures. Proposed, for the owner:
  hold a part to 10% on **CPU s/MB** by UDP, where the rate cannot hold anything to 10%, and to
  10% on both figures by the relay. **Not accepted** (owner, 2026-11-14): "not more than 10%
  slower overall compared to pf7." The gate is the rate, as the requirement says; the processor
  figure is reported beside it and holds nothing. So the measurement had to be made able to
  decide a 10% difference in an unsteady rate: `tools/speed.py --against` runs the reference and
  the programs under test turn and turn about, 15 of each, and compares medians. A profile of pf.7 followed the next day (the entry above). Nothing compares gosh with these figures until a part
  can carry a forward (queue item 5).

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
- **2026-11-15:** it does. `src/meetup/passing-test.cc` (`test_replay`) and `proof-test.cc`, with
  the real cipher and no socket: with twelve bytes copied from one datagram, someone without the
  key has a pair of their own passed on a day later, has a datagram delivered to an end of the
  pair they saw, and takes that pair's other end's place. So the finding is **demonstrated**,
  at the rule; and it is still not fixed (`src/meetup/README.md`).

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
