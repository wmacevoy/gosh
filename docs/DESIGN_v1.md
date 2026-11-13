# Design — v1

The parts of gosh, how they meet, and the seam. Queue item 1 (`REQ6/SHAPE-5`, `REQ7/SHAPE-1`).

**Status: accepted by the owner on 2026-11-13.**
Drafted by the agent the same day. Nothing here is built, so nothing here has been run
([SE1/A2](../vendor/se1/guide/10-agents.md#a2)).

**What it was made from.** Read in full: the fork's changes to upstream's own files
(`git -C ../mosh-dev/mosh diff decd9b7 mosh-1.4.0-pf.7`, the 27 changed files), the eleven headers
the fork added, and `../mosh-dev/docs/DESIGN_v1.md`. Searched, not read: the added `.cc` files, for
what they include, where they read the environment, and where they read a clock. So a line below
about what a part *is* comes from its header and the old design; a line about what it *calls* comes
from its includes.

The state table, the essential complexity and the decisions made while building the fork are in
`../mosh-dev/docs/DESIGN_v1.md` and still describe what gosh must do. Each is carried into this
document when its part lands, checked against the code then, and not copied now.

## The map

Which part may call which. An arrow is "calls, or starts". Nothing may call against an arrow, and a
part may call nothing that is not at the end of one of its arrows.

```
  the extension            (its own repository, ../remote-gosh)
       │ runs
       ▼
  the command  ◄─ puts in place ─  install
       │ becomes
       ▼
  the seam: the programs   (gosh-client, gosh-server: the core's programs, with hooks)
       │
       ├──► forwarding ──► the seam: the side channel ──► session (the core)
       ├──► copy
       ├──► relay ──► path-finding ──► meetup
       ├──► meetup                    (gosh-server meetup: a process of its own)
       └──► session (the core)
```

Four things the picture does not show:

- **The seam appears twice, and is one part.** It is every place gosh's code meets the core: hooks
  in the core's two programs, which call the parts; and the side channel in the core's transport,
  which forwarding calls. Listed file by file below.
- **The core calls nothing of gosh's** except through a hook the seam lists.
- **Relay and the session never call each other.** They are separate processes and meet as UDP
  datagrams on loopback; the one call is the seam's "send to this address from now on".
- **Copy and forwarding never call each other.** Forwarding carries a stream between two
  descriptors; copy is a process holding one. The programs join them.

Any part may also use these of the core's, which hold no session state: its crypto (sealing with a
key), `Addr`, `NetworkException`, its random numbers and byte order. Not its clock: see *Edges*.

## Parts

The ten names are the client's (requirements, *Scope decisions*, "The parts are named"). File names
are the fork's at `mosh-1.4.0-pf.7`, under `src/`, and are where each part's code comes from.

| Part | What it is for | Comes from | Serves | Its tests come from |
| ---- | -------------- | ---------- | ------ | ------------------- |
| session (the core) | mosh's protocol and terminal: upstream's code at a pinned commit, untouched | upstream at `decd9b7` | `REQ7/SHAPE-1`, `REQ6/UP-2` | upstream's own suite |
| the seam | the listed changes the core needs: the side channel and capabilities in the transport; options, headless, and hooks in the two programs | the 27 changed files, below | `REQ7/SHAPE-1`, `REQ1/CMD-3`, `REQ1/FWD-5` | `forward-e2e` (`-N`), `headless-smoke.py`; a stock mosh at one end (new, queue item 3) |
| forwarding | forwards and the stream: TCP and UDP ports, and one byte channel, carried over the side channel | `network/forwarding.h`, `udpforward.*`, `tcpforward.*`, `tcpadapter.*`, `tcpinudp.*` | `REQ1/FWD-1`, `REQ1/FWD-2`, `REQ1/FWD-3`, `REQ1/FWD-4` | `tcp-forward`, `tcp-in-udp`, `udp-forward`, `forward-e2e`; four fuzzers |
| relay | carries a session's datagrams inside ssh when UDP does not answer; decides, per datagram, which path is used | `network/relay.*`; and the 3 s and 10 s of `consider_relay` in `frontend/stmclient.cc` | `REQ1/PATH-1`, `REQ1/PATH-2`, `REQ1/SHAPE-2` | `relay.test`, `headless-smoke.py`, `fake-ssh.py` |
| path-finding | finds a punched path, or one across a local network: STUN, candidates, probes | `network/punch.*` (class `Punch`, the STUN functions) | `REQ1/PATH-3`, `REQ1/PATH-6`, `REQ1/PATH-7` | `nat-lab.py` (`easy`, `moved`, `room`), `punch_fuzzer` |
| meetup | the public machine: answers STUN for holders of its key and passes their datagrams on; and the key's proof | `network/punch.*` (`meetup_serve`, `meetup_proof`) | `REQ1/PATH-4`, `REQ1/PATH-5` | `nat-lab.py` (`passed`), `udp-forward` (key proof) |
| copy | one end of a copy over a stream; and the process at each end of any stream | `network/filecopy.*`, `network/streamchild.*` | `REQ1/CP-1` to `REQ1/CP-7` | `file-copy`, `mocp.test`, `file_copy_fuzzer` |
| the command | `gosh` and `gocp`: log in by ssh, start the server, become the client | `frontend/moshwrapper.*` | `REQ1/CMD-1`, `REQ6/NAME-1` | `wrapper.test` |
| install | builds, packages, checks and puts a release in a home directory | `scripts/install.sh`, `scripts/package-release.sh`, `.github/workflows/release.yml` | `REQ1/CMD-2`, `REQ6/CMD-4`, `REQ1/SHAPE-3` | none in the fork (trace: "run by hand"); queue item 10 |
| the extension | the editor's side, in `../remote-gosh` | `../remote-mosh` at `9ff0118` | `REQ1/EXT-1`, `REQ4/EXT-2`, `REQ1/EXT-3`, `REQ1/EXT-4` | `remote-mosh/test/` |

Not carried, and why: `scripts/mosh.pl` (non-goal: the Perl script is not migrated); the fork's
`docs/`, `README.md` and man pages (gosh writes its own, queue item 10); the fork's CI workflow
(queue item 3 makes gosh's).

## The seam, file by file

Every file of upstream's that the fork changed, `decd9b7` to `mosh-1.4.0-pf.7`, with lines added and
removed. This is the whole list: 27 files, of which 12 are source.

**In the transport: 8 files, 175 lines added.** This is the side channel, and it is small.

| File (`src/`) | + / − | What changes | For |
| ------------- | ----- | ------------ | --- |
| `protobufs/transportinstruction.proto` | 8 / 0 | two optional fields in mosh's `Instruction`: `capabilities` (8), `recv_loss` (9) | `REQ1/CMD-3`, `REQ7/SHAPE-1` |
| `network/transportsender.h`, `-impl.h` | 9 / 2 | holds the capabilities; puts them, and the loss it measures, in each instruction | `REQ1/CMD-3` |
| `network/transportfragment.cc` | 2 / 0 | a changed capability or loss counts as a new instruction | `REQ1/CMD-3` |
| `network/networktransport.h`, `-impl.h` | 78 / 3 | a payload beginning `0xFF` goes to a sink and not to state synchronisation; sending one; what the peer can do; how much the peer is losing; "send to this address"; "last heard" | forwarding (`REQ1/FWD-1`, `REQ1/FWD-2`); relay (`REQ1/PATH-1`) |
| `network/network.h`, `network.cc` | 78 / 3 | three separate things: measuring loss of what is received (`REQ1/FWD-2`); the client sending somewhere else, and "last heard" (`REQ1/PATH-1`); and the IPv6 socket fix, which is a named exception to `REQ7/SHAPE-1` until upstream has it (pull request 1442, open on 2026-11-13) | as said |

**In the two programs: 4 files, 584 lines added.** This is most of the seam, and the part to shrink.

| File (`src/frontend/`) | + / − | What changes | For |
| ---------------------- | ----- | ------------ | --- |
| `mosh-client.cc` | 162 / 4 | becomes the command when run under its name; says so when a dropped variable is set; options `-L -R -LU -RU -F -N -X`; reads the relay's two variables; exit message and status when headless or for a stream | the command, forwarding, relay, copy |
| `stmclient.h`, `stmclient.cc` | 265 / 23 | headless: no terminal read or drawn, `connected; forwarding.` said once (`REQ1/FWD-5`); forwarding's descriptors, reads and timers in the loop; starting the relay after 3 s or 10 s of quiet (`REQ1/PATH-1`); starting and waiting on a stream's process; says when the server is too old for a stream (`REQ1/CMD-3`) | forwarding, relay, copy |
| `mosh-server.cc` | 157 / 7 | first words `meetup`, `meetup-key`, `relay`, and the dropped `stun` refused; the forwarding options; forwarding in the loop; a week's timeout for a stream session | meetup, relay, forwarding, copy |

**In the build and upstream's tests: 8 files.** `configure.ac` (version, `ln -s`); `Makefile.am` in
`src/frontend`, `src/network`, `src/fuzz`, `src/tests`, `man`, `scripts` (the added files and the
two extra names); `src/tests/e2e-test-subrs` (5 lines: upstream's tests ask no STUN server,
`REQ1/PATH-7`).

**Changed in the fork and not part of the seam: 7 files.** `README.md`, three man pages,
`scripts/mosh.pl`, `.github/workflows/ci.yml` and `release.yml`: not carried, as above.

One thing requirements v1 to v6 named that was not found: their exceptions listed a "client
need not time out while headless" change. No change to a timeout is in the client's diff. What is
there is headless itself, and `REQ7/SHAPE-1` now says so. See *Questions*.

## Edges

`REQ6/SHAPE-5` never passes a part with `getenv` or a clock read inside its logic. Found at pf.7 by
search, so each part's landing knows what it has to move to the edge:

- **Environment.** `relay.cc` reads eight variables in the middle of the relay (the punch, STUN and
  meetup settings, the log path, a test-only idle time, `SSH_CONNECTION` twice). `filecopy.cc`
  reads `HOME`; `streamchild.cc` reads `HOME` and `SHELL`. Forwarding and path-finding read none.
- **Clocks.** `timestamp()` is read inside `tcpadapter.cc`, `tcpforward.cc`, `udpforward.cc`,
  `punch.cc`, `relay.cc` and `filecopy.cc`, two or three places each. `Punch` and the adapter's
  port already take `now` as an argument; the reads are in what calls them.

The shape each part takes: logic that is given `now`, bytes and options and returns bytes and what
to do next; and an edge, a few lines, that owns the sockets, reads the clock, and parses the
environment once into an options value when the program starts.

## Decisions

Proposed by the agent and accepted by the owner on 2026-11-13. "Rejected" is what else was weighed.

- **The core is a git submodule at `core/`, at upstream's `decd9b7`.** `REQ6/UP-2`. Byte for byte
  is then git's own claim, and the check is two facts: the submodule is at the commit the design
  names, and its tree is clean. Taking upstream's fix is moving the pointer. `decd9b7` because every
  measurement of pf.7 was made on it; it was upstream's newest on the day the fork's copy was last
  fetched, which was not repeated today. Rejected: a copy of upstream's tree with a list of hashes
  (a second thing to keep true, and 226 files in a diff nobody reads).
- **The seam is patch files in `seam/`, applied at build time to a copy of the core.** `REQ7/SHAPE-1`.
  One patch per concern: capabilities; the side channel; loss; "send elsewhere"; the IPv6 fix; then
  one per program. A patch that no longer applies stops the build, so moving the pin says at once
  what the seam costs. Rejected: gosh's own copies of the changed files (about 4,800 lines of
  upstream's become gosh's and stop receiving upstream's fixes); subclassing the transport from
  outside (what the seam needs is private there).
- **The programs' patches shrink to call sites.** `REQ6/SHAPE-5`. Today the bodies are in the
  patch: `engage_relay`, `consider_relay`, `tend_stream`, the headless announcement, the option
  parsing. As each part lands its body moves into the part, and the patch keeps the line that calls
  it. The count of patch lines per program is reported with each part, and should fall.
- **Deciding when to start the relay belongs to relay, not to the client program.** `REQ1/PATH-1`.
  The 3 and 10 seconds are in `stmclient.cc` today, and the 2.5 seconds are in `relay.cc`; one
  function of plain numbers holds all three, and queue item 7's unit test drives it.
- **Virtual calls stay until a profile points at one.** `REQ6/SPEED-1`. Each forwarded datagram
  crosses three (`SideChannelSink`, `SegmentSink`, `SegmentPort`). Not measured; templates there
  are the client's stated remedy if `tools/speed.py` shows a part more than 10% under pf.7.
- **A name of the core's own stays mosh's.** `REQ6/NAME-1` *(the agent's reading)*: `MOSH_KEY`,
  `MOSH CONNECT`, the prediction variables are what a stock mosh at the other end speaks, and are
  not "something of gosh's own".
- **gosh's code is in one directory per part, named for the part,** with its tests beside it, and
  the check command refuses an include that goes against the map. Rejected: leaving it all in the
  core's `src/network/`, which is how copy came to sit in a networking library.

## Questions for the owner, and his answers

Asked and answered on 2026-11-13: "fine with the rest", of all but the fourth.

1. **Submodule and patches**, which add a top-level `core/` and `seam/` and a build step. — **Yes.**
2. **Fields 8 and 9 of mosh's `Instruction` are gosh's by squatting.** If upstream ever gives either
   number another meaning, a gosh and a stock mosh misread each other, which is `REQ7/SHAPE-1`'s
   veto. Since gosh need not work with the fork's releases, the numbers can still move, and cannot
   once gosh has a release. — **Yes:** both move to a high number when the seam lands (queue
   item 5), and the number is written here then as a one-way door.
3. **The glossary and the part names differ.** `forwarding`, `path-finding` and `install` are part
   names in *Scope decisions* and not rows of the glossary; "the command" is "the mosh command"
   there; and "side channel", the seam's one interface, is in neither. — **Yes:** done in
   requirements v7.
4. **"client need not time out while headless"**, which the requirements listed as an exception up to v6.
   — **Not known**: "not sure where that came from --- -N is how the remote works so maybe
   something about how reconnects work as a user moves there." What a search found: the words are
   in requirements v1, which the agent rebuilt from the code after the fact, and have been carried
   unchanged since. No commit of the fork changes a timeout in the client. The two nearest are in
   other places: the server gives a stream session a week before it gives up (`1aa2bdc`), and a
   forwarded connection is kept through any loss of contact (`5b65538`). So the exception names
   nothing that is in the seam, and the seam table above stands without it. Requirements v7 has
   the exception say "headless (`-N`)", which is there.
5. **Which server `gosh` asks a host for.** — **Yes:** `gosh-server`, then `mosh-server` if the
   host has none, saying once which it got.
