# Design — v1

**Superseded by `DESIGN_v2.md`** (2026-11-13). Kept as it was written; its seam table, its tests column and its record of the owner's answers are still cited from v2.

The parts of gosh, how they meet, and the seam. Queue item 1 (`REQ6/SHAPE-5`, `REQ10/SHAPE-1`).

**Status: accepted by the owner on 2026-11-13, then corrected the same day after a review**
(`FINDINGS.md`, 2026-11-13). The seven decisions he accepted stand, two of them reworded where the
code showed their wording was wrong (the patches are an ordered series; starting the relay is two
decisions). What the review added that was his to decide he answered the same day (*Questions*, 6 to 10). Nothing here is built, so nothing here has been run
([SE1/A2](../vendor/se1/guide/10-agents.md#a2)).

**What it was made from.** The first draft read the fork's changes to upstream's own files and the
added headers, and only searched the added `.cc` files. The review then read every added `.cc`
file, the tests and the release scripts at `mosh-1.4.0-pf.7`, one reader per part, with a second
reader trying to refute each finding. Line numbers below are at that tag, under `src/`.

The state table, the essential complexity and the decisions made while building the fork are in
`../mosh-dev/docs/DESIGN_v1.md` and still describe what gosh must do. Each is carried into this
document when its part lands, checked against the code then, and not copied now.

## The map

Which part may call which. An arrow is "calls, or starts". Nothing may call against an arrow, and a
part may call nothing that is not at the end of one of its arrows.

```
  the extension            (its own repository, ../remote-gosh)
       │ runs gosh, gocp
       ▼
  the seam: the programs   (gosh-client and gosh-server: the core's programs, with hooks;
       │                    gosh and gocp are gosh-client under another name;
       │                    all put in place by install)
       ├──► the command
       ├──► forwarding ──► the seam: the side channel ──► session (the core)
       ├──► copy
       ├──► relay ──► path-finding ──► meetup
       ├──► meetup                    (gosh-server meetup: a process of its own)
       └──► session (the core)
```

What the picture does not show:

- **The seam appears twice, and is one part.** It is every place gosh's code meets the core: hooks
  in the core's two programs, which call the parts; and, in the core's transport, the side channel
  that forwarding calls (`SideChannelLink`), with two calls beside it that the client program makes
  for the relay ("send to this address from now on", "when was the server last heard").
- **The core calls nothing of gosh's** except through a hook the seam lists.
- **The command is called by the program, not the other way.** It is compiled into gosh-client.
  Run as `gosh` or `gocp`, the program's first hook calls the command, which logs in by ssh, starts
  the server, and returns the client's arguments and environment; the same process then carries on
  as the client. There is no exec. The command has a second entry: ssh runs gosh-client again as
  its ProxyCommand (`--mosh-fake-proxy`), to reach the host and say which address it reached.
- **The command meets copy, relay and path-finding only as words.** It calls none of them. It
  writes the client's options, the words a stream's processes are started from (`@copy send …`,
  `@sh`, `@stdio`, joined by the character 0x1F, carried in `-X` to both programs), and five
  variables (`MOSH_RELAY`, `MOSH_RELAY_COMMAND`, `MOSH_PUNCH`, `MOSH_STUN`, `MOSH_MEETUP`). Three
  of those pass through the client program unread and are read by the relay. These words are a
  contract between parts, and half of it crosses to another machine; it is written into this
  document when copy and relay land, with a test that what the command writes is what they parse.
- **Relay and the session never call each other.** They are separate processes and meet as UDP
  datagrams: on loopback at the client; at the server, at whichever of its own addresses answers.
  Relay is two processes: the local relay, forked from gosh-client without exec, with ssh as its
  child; and `gosh-server relay` on the server, which detaches from its ssh connection and, while
  it is one end of a punched path, outlives it by up to 60 s of silence.
- **Both relays stay in every path but the direct one.** A punched path, or one through a meetup,
  is client → local relay → path-finding → remote relay → server. The relays also carry
  path-finding's own words on the ssh stream (`HELLO`, `STUN`, `STUNKEY`, `KEY`, `PORT`, `CAND`).
- **Copy and forwarding never call each other, but forwarding starts copy's process at the
  server.** At the client the program starts the process and gives forwarding the descriptor. At
  the server the program gives forwarding a function, and forwarding calls it when the stream's
  connection arrives: forwarding decides when, never what.

Any part may also use these of the core's, which hold no session state: its crypto (sealing with a
key), `Addr`, `NetworkException`, its random numbers and byte order, and its generated `config.h`.
Not its clock: see *Edges*. A known hole: `Addr`, `NetworkException`, the clock and `Connection`
are all declared in the core's one header, `network/network.h`, so a check on includes alone cannot
see a part reaching for the session or the clock; the check must look for those names too.

## Parts

The ten names are the client's (requirements, *Scope decisions*, "The parts are named"), and each
is a word of the glossary since requirements v7. File names are the fork's at `mosh-1.4.0-pf.7`,
under `src/`, and are where each part's code comes from. Tests are marked **(b)** when they drive
the programs from outside and **(u)** when they link the fork's classes directly; the difference
matters for what "landed" can mean (*Questions*, 7).

| Part | What it is for | Comes from | Serves | Its tests come from |
| ---- | -------------- | ---------- | ------ | ------------------- |
| session (the core) | mosh's protocol and terminal: upstream's code at a pinned commit, untouched | upstream at `decd9b7` | `REQ10/SHAPE-1`, `REQ6/UP-2` | upstream's own suite |
| the seam | the listed changes the core needs: the side channel, capabilities and loss in the transport; options, headless, and hooks in the two programs | the 27 changed files and one added header, `network/sidechannel.h`, below | `REQ10/SHAPE-1`, `REQ8/CMD-3`, `REQ1/FWD-5` | (b) `forward-e2e` (`-N`); (b) `headless-smoke.py` with no relay mode, which at pf.7 only the Windows CI jobs run; (u) `udp-forward` (`test_loss_measurement`); a stock mosh at each end in turn (new: queue items 3 and 5). `REQ8/CMD-3`'s message: no test in the fork |
| forwarding | forwards and the stream: TCP and UDP ports, and one byte channel, carried over the side channel | `network/forwarding.h`, `udpforward.*`, `tcpforward.*`, `tcpadapter.*`, `tcpinudp.*`; `SideChannelMux` from `sidechannel.h` | `REQ1/FWD-1`, `REQ1/FWD-2`, `REQ1/FWD-3`, `REQ1/FWD-4` | (u) `tcp-forward`, `tcp-in-udp`, `udp-forward` (`test_spec`, `test_wire`, `test_replay`, `test_fragments`, `test_relay`, `test_remote`), four fuzzers; (b) `forward-e2e`. `REQ1/FWD-3` has only (u) tests. `REQ1/FWD-4`: the specification's default only; nothing checks what a listener binds |
| relay | carries a session's datagrams inside ssh when UDP does not answer; decides, per datagram, which path is used; starts path-finding and carries its words | `network/relay.*`; and `consider_relay` and `engage_relay` in `frontend/stmclient.cc` | `REQ1/PATH-1`, `REQ1/PATH-2`, `REQ1/SHAPE-2`; and of `REQ1/PATH-7`, whether anyone is asked and who (`MOSH_PUNCH`, `MOSH_STUN`, `MOSH_MEETUP`, the default public servers) | (b) `relay.test`, which runs `headless-smoke.py` (`always`, `blocked`, `translated`, `punched`) with the remote relay started directly; (b) the `--relay=always` case of `wrapper.test`, through `fake-ssh.py`, the only test that starts it as ssh does; (u) `udp-forward` (`test_relay_frames`). `REQ1/PATH-2` has no test at pf.7: its test is commit `6c0cc61` on the fork's branch `refactor/relay`, which changes tests only |
| path-finding | finds a punched path, or one across a local network: STUN, candidates, probes; and the ends' side of a path through a meetup | `network/punch.*` (class `Punch`, the STUN functions) | `REQ1/PATH-3`, `REQ1/PATH-6`; the ends' side of `REQ1/PATH-4`; of `REQ1/PATH-7`, only that it asks nobody when given an empty list | (u) `udp-forward` (`test_punch`: two ends and a stranger on loopback, with the clock passed in); (b) `relay.test` (`punched`); (b) `nat-lab.py` (`easy`, `moved`, `room`), which needs root and is not in `make check`; `punch_fuzzer` (the STUN and address parsers only). `REQ1/PATH-7`: no test, since every test sets `MOSH_STUN` |
| meetup | the public machine: answers STUN for holders of its key and passes their datagrams on; the key's proof, and the layout of a passed-on datagram | `network/punch.*` (`meetup_serve`, `meetup_proof`, `meetup_proof_ok`, `MEETUP_MARK`, `MEETUP_HEADER`) | `REQ1/PATH-4`, `REQ1/PATH-5` | (b) `nat-lab.py` (`passed`), root only; (u) the key-proof lines of `test_punch`. `meetup_serve` is not fuzzed |
| copy | one end of a copy over a stream; and the process at each end of any stream | `network/filecopy.*`, `network/streamchild.*` | `REQ1/CP-2`, `REQ1/CP-3`, `REQ1/CP-4`, `REQ1/CP-6`; with the command, `REQ1/CP-1`, `REQ1/CP-5`, `REQ1/CP-7` | (u) `file-copy`, `file_copy_fuzzer`; (b) `mocp.test`; (b) the two `-X @copy` checks of `headless-smoke.py`, run only with no relay mode |
| the command | `gosh` and `gocp`: log in by ssh, start the server, return the client's arguments; the command line of a copy, of `--stdio` and of the path options; and ssh's ProxyCommand | `frontend/moshwrapper.*` | `REQ1/CMD-1`, `REQ6/NAME-1`; the command-line half of `REQ1/CP-1`, `REQ1/CP-5`, `REQ1/CP-7`, `REQ1/PATH-7`, and of `REQ1/PATH-1` (the fallback's ssh command, `BatchMode=yes`) | (b) `wrapper.test`, its built-in half: it runs every case twice, the second time against the Perl script, and skips without Perl |
| install | builds, packages, checks and puts a release in a home directory | `scripts/install.sh`, `scripts/package-release.sh`, `.github/workflows/release.yml` | `REQ1/CMD-2`, `REQ6/CMD-4`, `REQ1/SHAPE-3` | no test of `install.sh` (trace: "run by hand"). Checks at each release, to keep: the packager refuses a program that needs a library the system does not ship, and runs the four names; the installer refuses a wrong checksum and a client that does not run |
| the extension | the editor's side, in `../remote-gosh` | `../remote-mosh` at `9ff0118` | `REQ1/EXT-1`, `REQ4/EXT-2`, `REQ1/EXT-3`, `REQ1/EXT-4` | `remote-mosh/test/` |

`udp-forward` is one test program holding the unit tests of four parts and of the seam. It is split
by part when the first of them lands, because the map would refuse it whole.

Not carried, and why:

- `scripts/mosh.pl` (non-goal: the Perl script is not migrated). With it go the second half of
  `wrapper.test`, the packager's line that ships it, and `scripts/Makefile.am`'s change, which is
  almost all about it. The command's own help still tells the user about Perl; that is left behind.
- The fork's `docs/`, `README.md` and man pages. gosh writes its own, and no queue item's *done*
  covered that until queue item 10 was given a clause for it (*Questions*, 10).
- The fork's CI workflow (`ci.yml`); gosh has its own. `release.yml` is not seam but is carried, as
  install's source: it alone makes the self-contained builds and the checksums the installer checks.

## The seam, file by file

Every file of upstream's that the fork changed, `decd9b7` to `mosh-1.4.0-pf.7`, with lines added and
removed: 27 files, of which 12 are source. And one file the fork added: `network/sidechannel.h`
(137 lines), which the patched transport includes and inherits from. It defines the side channel's
two interfaces, the `0xFF` mark, the 10 s after which the peer counts as out of contact, and the
capability bits; it goes into the copy of the core with the side-channel patch.

**In the transport: 8 files, 175 lines added in the fork, 168 of them gosh's.** The side channel
and what travels with it. Small.

| File (`src/`) | + / − | What changes | For |
| ------------- | ----- | ------------ | --- |
| `protobufs/transportinstruction.proto` | 8 / 0 | two optional fields in mosh's `Instruction`: `capabilities` (8), `recv_loss` (9) | `REQ8/CMD-3`, `REQ10/SHAPE-1` |
| `network/transportsender.h`, `-impl.h` | 9 / 2 | holds the capabilities; puts them, and the loss it measures, in each instruction (loss only when there are capabilities) | `REQ8/CMD-3` |
| `network/transportfragment.cc` | 2 / 0 | a changed capability or loss counts as a new instruction | `REQ8/CMD-3` |
| `network/networktransport.h`, `-impl.h` | 78 / 3 | the transport is a `SideChannelLink`; a payload beginning `0xFF` goes to a sink and not to state synchronisation; sending one, only once the peer has advertised; what the peer can do; how much the peer is losing; "in contact", from "last heard"; "send to this address"; copying the transport made private | forwarding (`REQ1/FWD-1`, `REQ1/FWD-2`, `REQ1/FWD-3`); relay (`REQ1/PATH-1`) |
| `network/network.h`, `network.cc` | 78 / 3 | measuring loss of what is received (`REQ1/FWD-2`); "last heard", which keeps a forwarded connection through an outage (`REQ1/FWD-3`) and which the relay decision reads (`REQ1/PATH-1`); the client sending somewhere else (`REQ1/PATH-1`); and the IPv6 socket fix (7 added, 1 removed), which **gosh does not carry** since requirements v10: it stays in the fork and comes with the pin when upstream has it | as said |

**In the two programs: 4 files, 584 lines added.** Most of the seam, and the part to shrink.

| File (`src/frontend/`) | + / − | What changes | For |
| ---------------------- | ----- | ------------ | --- |
| `mosh-client.cc` | 162 / 4 | calls the command when run under its name, and is ssh's ProxyCommand when run as `--mosh-fake-proxy`; says so when a dropped variable is set; options `-L -R -LU -RU -F -N -X`; reads the relay's two variables; exit message and status when headless or for a stream | the command, forwarding, relay, copy |
| `stmclient.h`, `stmclient.cc` | 265 / 23 | headless: no terminal read or drawn, `connected; forwarding.` said once (`REQ1/FWD-5`); forwarding's descriptors, reads and timers in the loop; starting the relay after 3 s or 10 s of quiet (`REQ1/PATH-1`); starting and waiting on a stream's process; says when the server is too old for a stream (`REQ8/CMD-3`); the privilege drop (OpenBSD `pledge`) not made at all for a stream, and widened to start processes unless the relay is off | forwarding, relay, copy |
| `mosh-server.cc` | 157 / 7 | first words `meetup`, `meetup-key`, `relay`, and the dropped `stun` refused; the forwarding options; forwarding in the loop; gives forwarding the function that starts a stream's process; a week's timeout for a stream session; the privilege drop not made for a stream | meetup, relay, forwarding, copy |

**In the build and upstream's tests: 8 files.** `configure.ac` (version, `ln -s`); `Makefile.am` in
`src/frontend`, `src/network`, `src/fuzz`, `src/tests`, `man`, `scripts`; `src/tests/e2e-test-subrs`
(5 lines: upstream's tests ask no STUN server, `REQ1/PATH-7`). gosh's patch for these is written
for gosh and not taken from the fork, whose `src/network/Makefile.am` puts every added file into the
core's networking library.

**Changed in the fork and not part of the seam: 7 files.** `README.md`, three man pages,
`scripts/mosh.pl`, `.github/workflows/ci.yml`: not carried. `.github/workflows/release.yml`:
install's, as above.

Two things the carried code does not do, found by reading it:

- **`REQ8/CMD-3` is not met for a plain forward** (v8 was written because of it). The only message for an older peer is for a
  stream. With `-L`, `-R`, `-LU` or `-RU` to a peer that never advertises the side channel, the
  client says nothing: with `-N`, `connected; forwarding.` never comes; with a terminal, the forward
  carries nothing, silently. Read, not run. With a stock mosh at the other end now required, this
  is the ordinary case and not a rare one (*Questions*, 6).
- **Requirements v1 to v6 named a seam entry that is not there**: a "client need not time out while
  headless" change. No change to a timeout is in the client's diff; the requirement has said "headless" since v7.

## Edges

`REQ6/SHAPE-5` never passes a part with `getenv` or a clock read inside its logic, and asks, as a
matter of judgement, for logic apart from sockets too. What each part has to move, at pf.7:

- **Environment.**
  - `relay.cc` reads seven variables in eight places. Five once, in the forked local relay: the
    punch, STUN and meetup settings, the meetup's key, a test-only idle time. Three of those are
    put there by the command and are today's interface from the command to relay; the options
    value that replaces them is what enforces `REQ1/PATH-7`. `SSH_CONNECTION` (twice) is an input
    from sshd on the server, not a setting, and cannot move to the client's start. The log path is
    read on every log line.
  - `filecopy.cc` reads `HOME` at each use, not once; `streamchild.cc` reads `HOME` and `SHELL`.
  - `moshwrapper.cc` (the command) reads `PATH`, the prediction setting, fifteen locale variables
    and `HOME`, and sets ten variables in the middle of its work, for the client and the relay.
  - Forwarding and path-finding read none.
- **Clocks.** There are four different clocks, and a landing's search must look for all of them.
  - The core's `timestamp()`, which is the event loop's frozen value: `tcpadapter.cc` (2),
    `tcpforward.cc` (3), `udpforward.cc` (2). The adapter's port takes `now` for its timers but
    reads the clock itself in `read_fd` and `from_network`, because the interface between the
    forwarder and the port has no `now` on those two; moving them changes that interface. The UDP
    forwarder takes `now` nowhere. "In contact" is one more clock read, behind the seam.
  - `relay.cc`'s own `now_ms()`: read twice, once in each of its loops; everything below takes `now`.
  - `filecopy.cc`'s own `now_ms()`: six places, and `time(NULL)` once.
  - `time(NULL)` twice in `punch.cc`, both in the meetup's serving loop. `Punch` itself takes `now`
    on every entry and reads no clock.
- **Sockets, processes and lookups.** The larger move, and the one the first draft left out.
  - Forwarding: the adapter's TCP state machine makes its own system calls (`accept`, `connect`,
    `send`, `recv`, `shutdown`, `poll`), as does the UDP forwarder; both resolve names and bind in
    their constructors. For forwarding the clock is the small move and the sockets are the large one.
  - Path-finding: `Punch`'s constructor opens its own socket, reads the machine's interfaces
    (`REQ1/PATH-6`), and forks twice, without exec, so that name lookups for STUN servers do not
    hold up datagrams. The copy lives about a second and holds every descriptor the relay has open.
  - Copy is not shaped like the others: its logic is two blocking procedures that read and write
    the stream, the clock and the file system from several depths. Making its logic "given bytes,
    returns bytes" is a rewrite of its control flow, not a move of a few lines.

The shape each part takes: logic that is given `now`, bytes and options and returns bytes and what
to do next; and an edge that owns the sockets and processes, reads the clock, and parses the
environment once into an options value. For copy, how far to go is to be decided when it lands.

## Decisions

Proposed by the agent and accepted by the owner on 2026-11-13; the second and fourth reworded after
the review, in substance unchanged. "Rejected" is what else was weighed.

- **The core is a git submodule at `core/`, at upstream's `decd9b7`.** `REQ6/UP-2`. Byte for byte
  is then git's own claim, and the check is two facts: the submodule is at the commit the design
  names, and its tree is clean. Taking upstream's fix is moving the pointer. `decd9b7` because every
  measurement of pf.7 was made on it; it was upstream's newest on the day the fork's copy was last
  fetched, which was not repeated today. Rejected: a copy of upstream's tree with a list of hashes
  (a second thing to keep true, and 226 files in a diff nobody reads).
- **The seam is patch files in `seam/`, applied at build time, in order, to a copy of the core.**
  `REQ10/SHAPE-1`. One patch per concern, as a series, each cut against the result of the one
  before:
  capabilities; the side channel, with `sidechannel.h` and "last heard"; loss; "send elsewhere";
  one per program; and one for the build and upstream's tests. They are not independent: at pf.7
  the concerns share hunks and single lines (loss is sent only inside the capabilities test; one
  initialiser line serves three). A patch that no longer applies stops the build, so moving the pin
  says at once what the seam costs. Each patch begins with what `REQ10/SHAPE-1` asks for: the
  pinned commit it is cut against, the requirement or part it is for, and, where it reshapes the
  core's code and does not only add to it, the file and function at the pin the code came from and
  why a small change would not do. The first such are the programs' loops, where a hook has to be
  made for a part to be called from. Rejected: gosh's own copies of the changed files (the changed
  files are about 4,800 lines at pf.7, and would stop receiving upstream's fixes); subclassing the
  transport from outside (what the seam needs is private there).
- **The programs' patches shrink to call sites.** `REQ6/SHAPE-5`. Today the bodies are in the
  patch: `engage_relay`, `consider_relay`, `tend_stream`, the headless announcement, the option
  parsing. As each part lands its body moves into the part, and the patch keeps the line that calls
  it. The count of patch lines per program is reported with each part, and should fall. Two things
  stay in the programs because they belong to no one part: the privilege-drop conditions; and the
  joining of a stream, where `tend_stream` and the stream's start call copy, forwarding and the
  session together. Of that, only the decision (has the process ended, are connections left, has
  the 5 s grace passed) moves into copy, as plain values.
- **When to use the relay is two decisions, and relay owns both as functions of plain numbers.**
  `REQ1/PATH-1`, `REQ1/PATH-2`. (a) Start the relay at all: 3 s with nothing ever heard, 10 s after
  something was. Today `consider_relay` in `stmclient.cc`, in the client process, once per session.
  (b) Use the ssh stream for this datagram: 2.5 s since either UDP route answered. Today in
  `relay.cc`, in the local relay process, on inputs the client cannot see, with the other timings
  of that choice beside it (the stream closed after 60 s unused, the remote relay's 60 s). After
  the first start only (b) is ever consulted. Queue item 7's unit test drives both.
- **Virtual calls stay until a profile points at one.** `REQ6/SPEED-1`. There are four interfaces.
  Every received side-channel payload crosses `SideChannelSink` twice (the mux, then the forwarder);
  every sent one crosses `SideChannelLink`, which the core's transport implements, so making that
  one a template changes the seam's patch. TCP forwarding adds `SegmentPort` on receive and
  `SegmentSink` on send; UDP forwarding crosses neither. Not measured. Templates are the client's
  stated remedy if the speed measurement (`../mosh-dev/tools/speed.py` until it is brought here)
  shows a part more than 10% under pf.7.
- **A name of the core's own stays mosh's.** `REQ6/NAME-1` *(the agent's reading)*: `MOSH_KEY`,
  `MOSH CONNECT`, the prediction variables are what a stock mosh at the other end speaks, and are
  not "something of gosh's own".
- **gosh's code is in one directory per part, named for the part,** with its tests beside it, and
  the check command refuses an include, or a name of the core's, that goes against the map.
  Rejected: leaving it all in the core's `src/network/`, which is how copy came to sit in a
  networking library.

## Questions for the owner, and his answers

Asked and answered on 2026-11-13: "fine with the rest", of all but the fourth.

1. **Submodule and patches**, which add a top-level `core/` and `seam/` and a build step. — **Yes.**
2. **Fields 8 and 9 of mosh's `Instruction` are gosh's by squatting.** If upstream ever gives either
   number another meaning, a gosh and a stock mosh misread each other, which is `REQ10/SHAPE-1`'s
   veto. Since gosh need not work with the fork's releases, the numbers can still move, and cannot
   once gosh has a release. — **Yes:** both move to a high number when the seam lands (queue
   item 5), and the number is written here then as a one-way door.
3. **The glossary and the part names differ.** — **Yes:** done in requirements v7.
4. **"client need not time out while headless"**, which the requirements listed as an exception up
   to v6. — **Not known**: "not sure where that came from --- -N is how the remote works so maybe
   something about how reconnects work as a user moves there." What a search found: the words are
   in requirements v1, which the agent rebuilt from the code after the fact, and were carried
   unchanged. No commit of the fork changes a timeout in the client. The two nearest are elsewhere:
   the server gives a stream session a week before it gives up (`1aa2bdc`), and a forwarded
   connection is kept through any loss of contact (`5b65538`). Requirements v7 has the exception
   say "headless (`-N`)", which is there.
5. **Which server `gosh` asks a host for.** — **Yes:** `gosh-server`, then `mosh-server` if the
   host has none, saying once which it got. *The review found this needs more than a yes: see 6.*

Questions 6 to 10 were raised by the review, and answered on 2026-11-13: "yes to all", to what
each recommends.

6. **A stock server at the other end, with forwards asked for.** pf.7 asks a host for one server
   and learns nothing about which it got; it passes the forward and stream options regardless. A
   stock `mosh-server` prints its usage for each option it does not know and starts anyway, and the
   client then says nothing (above: `REQ8/CMD-3` is not met for a plain forward). So answer 5 needs
   three things no requirement states: the remote line reports which server ran; to a stock server
   the command passes no forward, stream or relay options; and the user is told once, plainly, what
   will not work. Recommended: a clause of that requirement in its next version, built
   with queue item 5. It makes a requirement true; it is not a new feature. — **Yes:** requirements
   v8, `REQ8/CMD-3`; queue item 5.
7. **"First green against pf.7's programs" cannot be said of a unit test.** The (u) tests and the
   fuzzers link the fork's classes, whose interfaces `REQ6/SHAPE-5` is there to change; only (b)
   tests run unchanged against both pf.7 and gosh. `REQ1/FWD-3` and most of `REQ1/CP-2` to
   `REQ1/CP-6` have only (u) tests. Recommended: the queue's "landed" says it of (b) tests, and for
   a requirement with none a (b) test is written first, against pf.7, as part of queue item 4.
   — **Yes:** the queue's "landed" and item 4 say so.
8. **`REQ6/CMD-4`'s link may not be where a stock mosh looks.** A stock `mosh` asks a host for the
   bare name `mosh-server`. The installer puts everything in `~/.local/bin`, which the fork's own
   command adds to `PATH` itself because a command run by ssh often does not have it. So the link
   helps a stock client only on hosts whose non-login shell already has that directory. Inferred
   from the code, not run. Recommended: try it on one stock client and one bare host before queue
   item 10, and write what is found into the requirement's next version. — **Yes:** noted in
   queue item 10. Not yet tried.
9. **The speed measurement is not here.** The queue and the guide name `tools/speed.py`; it is in
   `../mosh-dev/tools`, and no item brings it. Recommended: queue item 4 brings it, made to take
   the programs it measures, and records pf.7's figures on this machine as the baseline.
   — **Yes:** queue item 4.
10. **No queue item covers gosh's own documentation**: a README for users, man pages, the student
   guide `REQ5/CLS-1` names. Recommended: a clause in queue item 10's *done*. — **Yes:** done.
11. **How far the core may be changed.** Not asked; the owner said it, as holder of the shape,
    on 2026-11-13: "shape-1 ---> changes tracked to the pin. if a refactor is warranted, say where
    it came from and why." Requirements v9, `REQ10/SHAPE-1`. It fits the patches decision and adds
    to it what each patch must say.
12. **The IPv6 socket fix.** Not asked; the owner, 2026-11-13: "the shape-1 exception can be in the
    mosh-dev version, let it go and write a handoff to mosh-dev about making that PR."
    Requirements v10: gosh's seam does not carry it, so the series of patches has no IPv6 patch.
    The cost is written in v10: on Windows, no session to a server at an IPv6 address until
    upstream has the fix and the pin moves (inferred, not run). The handoff is
    `../mosh-dev/HANDOFF.md`.
