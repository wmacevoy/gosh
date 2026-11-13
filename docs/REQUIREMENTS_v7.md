# Requirements — v7

What must be observably true, and what is not being built. Changes only with the client; a change in
meaning is a new version (`REQUIREMENTS_v8.md`); a spelling or lint fix is not. See
[SE1/R1–R11](../vendor/se1/guide/04-requirements.md).

Client: Warren MacEvoy, who is also the instructor whose students are the intended users, and so
their proxy ([SE1/H5](../vendor/se1/guide/09-people.md#h5)). The students have not been in the room.
· Date sent: 2026-11-13 · Drafted by the agent and accepted by the client the same day ("ok to all
this"), with one addition (CMD-4). v7 was drafted by the agent from the design's questions and
accepted by the client the same day ("make the changes and accept them"). *Tell us what is wrong
with it.*

**What changed from v6.** Two things, both found by writing the design (`DESIGN_v1.md`, *Questions
for the owner*), and nothing gosh does:

- the glossary gains the part names it lacked, **forwarding**, **path-finding** and **install**, and
  **side channel**; and "the mosh command" is **the command**, which is what the parts list already
  called it. Every part of the design is now a word here;
- SHAPE-1's second exception is **headless (`-N`)**. v1 to v6 said 'the "client need not time out
  while headless" change'. That wording came from v1's rebuild from the code; no change to a
  timeout in the client was found in the fork, and the client did not know its source ("not sure
  where that came from").

**What changed from v5** *(as v6 said it).* The client decided (2026-11-13) that this is its own project and no longer
a fork kept ready for upstream:

- it is **gosh**, "as in 'gosh, a better mosh!?'": "this is it's own thing now". Two new
  repositories, `gosh` and `remote-gosh`, "not a fork or clone". That a short name collides with
  others is accepted: "a small name will have a namespace collision --- i'm ok to live with that";
- the fork, `wmacevoy/mosh`, is frozen at `mosh-1.4.0-pf.7` and is what gosh is measured against,
  and what a class would use until gosh passes the same tests;
- **nothing is offered upstream.** "i think the upstream ends at ipv6", and the IPv6 fix need not be
  offered either: `mobile-shell/mosh` pull request 1442, opened by someone else on 2026-11-10,
  makes the same change and more. UP-1 is withdrawn;
- the code is to be migrated into **"comprehensible tested parts"**, with "a domain driven design
  model as a foundational shape" (SHAPE-5), and speed ahead of readability (SPEED-1);
- the working set moves into `gosh`, and requirement IDs may be written in its code and tests;
- the agent's reviews of the migration are accepted, and it is not called trusted until a third
  party has reviewed all of it (*Scope decisions*).

Nothing gosh does for a user is new: features stay frozen. The requirements below still write
pf.7's commands, messages and variables; NAME-1 says once, for all of them, what gosh calls each.

**How this was written.** Rebuilt from the code and from one long working conversation, after the
code existed ([SE1/W12](../vendor/se1/guide/08-work.md#w12)), with everything nobody had asked for
marked *(inferred)*; then read by the client, who corrected some of it (v2 to v4) and accepted the
rest (v5). It describes what `mosh-1.4.0-pf.7` and `remote-mosh` at `9ff0118` do, which is what
gosh is to do.

## Glossary

One line each. A new noun is a decision ([SE1/D9](../vendor/se1/guide/05-design.md#d9)). Settled by
the client on 2026-11-13.

| Term | Meaning |
| ---- | ------- |
| session | one mosh connection between a client and a server, with its key; it outlives changes of network |
| client, server | `mosh-client` on the user's machine; `mosh-server` on the machine worked on |
| gosh | this project: mosh's session, with forwards, copies, the relay and path-finding beside it *(since v6)* |
| upstream, mosh | `mobile-shell/mosh`. gosh carries its code and takes its fixes; it offers nothing back *(since v6)* |
| the fork | `wmacevoy/mosh`, frozen at `mosh-1.4.0-pf.7`: where gosh's code comes from, and what it is measured against *(since v6)* |
| the core | upstream's code as gosh carries it: untouched, at a pinned commit *(since v6)* |
| the seam | the listed places where gosh's code meets the core, and the changes it needs there *(since v6)* |
| part | a named piece of gosh with one purpose and a simple boundary; its name is a word in this glossary *(since v6)* |
| forwarding | the part that carries forwards and streams beside the session *(since v7)* |
| side channel | the seam's one interface: sealed datagrams between the two ends of a session that travel beside mosh's state synchronisation, and are neither acknowledged nor sent again *(since v7)* |
| forward | a TCP or UDP port at one end of a session whose traffic appears at the other (`-L`, `-R`, `-LU`, `-RU`) |
| stream | one reliable ordered byte channel between a process at each end of a session (`-X`) |
| copy | a file transfer over a stream, by `mocp` |
| the command | what logs in by ssh, starts the server and becomes the client: `mosh`, and under another name `mocp`; also the part that is it *("the mosh command" until v7)* |
| install | putting a release's programs into a home directory with one command; also the part that builds, packages and does it *(since v7)* |
| relay | carrying a session's datagrams inside an ssh connection when UDP cannot reach the server (`--relay`, `mosh-server relay`); also the two helper processes that do it |
| path | a way for a session's datagrams to travel between the two ends. Four, best first: direct; punched; through the meetup; through the relay |
| path-finding | the part that finds a punched path, or one across a local network, for a session on the relay *(since v7)* |
| punched path | a direct UDP path opened through NATs by both ends sending at once (`--punch`) |
| STUN | the question "what address do I appear to have from outside?", and a plain public server that answers it |
| meetup | a machine with a public address, run by the user, that answers the STUN question for holders of its key and, when the two ends can find no path to each other, passes their datagrams between them by UDP for as long as they need (`mosh-server meetup`). *"meetup does not imply how long you will stay" (client)* |
| headless | a session with no terminal (`-N`) |
| the extension | `remote-gosh`, for VSCodium (`remote-mosh` until it is migrated) |
| local copy | a folder on the host kept copied to the user's machine by the extension |

## Mission

- **MISSION-1** *(since v5)* *(judged)* A student can work on a remote machine from a laptop, in
  an editor, and keep working while they roam and lose connections, without an administrator and
  without understanding how. Judge: Warren, at the first class session run as CLS-1 describes.
  Veto: a student loses work because of it, or it exposes a student's machine or
  account.
  *Source: "remote codium via mosh, this was the prize"; "for students the all-in-one will be what
  they love"; and, of what done is: "30 students working together with the guide and a lab
  assistant all get an aws server where they can run the python web server, edit html, and see the
  result rendered locally as they edit remotely via the codium extension, which allows them to
  roam, lose connections, and still work."*

## Fundamental shape

Held by: Warren. The holder proposes a change; the client
makes it, in a new version.

- **SHAPE-1** *(since v7)* *(judged)* mosh's own protocol and the terminal are left alone. gosh
  carries the core untouched and pinned; what it changes there is the seam, and the seam is listed.
  Everything added rides beside the protocol and is negotiated, so that either end may be a stock
  mosh and the terminal still works. Judge: the holder, at each release. Veto: a test in which one
  end is a stock mosh fails. *(v1 said "an older mosh"; the client, 2026-11-13: gosh need not work
  with the fork's own earlier releases. v7 changes only its second exception, below.)*
- **SHAPE-2** *(since v1)* *(judged)* No decision to get wrong: where there are several
  ways to reach the other end, all are tried, the best that answers is used, and the better ones
  keep being tried. The user chooses nothing.
- **SHAPE-3** *(since v1)* *(judged)* It installs into a home directory with one
  command, needs no root, and adds no dependency a bare machine lacks.
- **SHAPE-4** *(since v1)* *(judged)* The documentation says what has not been tested
  as plainly as what has.
- **SHAPE-5** *(since v6)* *(judged)* The code is a model of the domain: "small parts that interact
  with simple boundaries and name what they are for (parts of requirements and design) so they can
  be found, understood, and updated." A part's name is the glossary's word for what it does; its
  logic is apart from sockets, clocks and the environment; and the design says which requirements
  it serves. Where this and speed disagree, speed wins (SPEED-1): "performance matters here, so
  readability is second to that"; "instead of polymorphism templates might be appropriate." Judge:
  the reviewer of each part (*Scope decisions*), and the third party at the end. Vetoes, which
  never pass it: a part with no glossary word; `getenv` or a clock read inside a part's logic.
  *(The agent's reading, accepted by the client 2026-11-13: the strategic half of domain-driven design — named parts, one
  vocabulary, a map of how parts meet — and not its entity and repository patterns on the path
  every datagram takes.)*
- **Foundation:** SE1.7
- **Says no to:** two-way file synchronisation; a TUN device; carrying forwarded data through
  mosh's state synchronisation. Each was considered and refused; reasons in
  `mosh/docs/design.md` and `mosh/docs/status.md`.
- **Exceptions:** SHAPE-1: the seam, whose first entries are the Windows IPv6 socket fix (until
  upstream has it) and headless (`-N`). SHAPE-3: none; the
  Perl script is not migrated.

## The pain

*(inferred)* A student logs in to a course server with ssh. The lid closes, or the Wi-Fi changes
between buildings, and the session is gone, with whatever was running in it. They log in again and
start over. To see a web page their program serves they need a second ssh with a forwarding option
they have not been taught; to get results home they need scp, which starts again from nothing when
interrupted. On some campus networks standard mosh does not connect at all, and it does not say
why. Nobody gets a call: the student concludes the tool is broken and stops using it.

## People this touches

- **Students** *(not in the room)*: on laptops running macOS, Windows or Linux, on campus, home and
  phone networks. Not all can install software as administrator; some have only password login.
- **The instructor**: chooses the tool, writes the handout, gets the questions.
- **Whoever runs the course server** *(not in the room)*: sees new long-lived processes, a UDP port
  range in use, repeated ssh logins from the fallback, and possibly a `mosh-server stun` listener.
- **Operators of public STUN servers** (Google, Cloudflare) *(not in the room)*: receive one small
  datagram from each end of a session that has fallen back to ssh, by default.
- **Other users of a shared machine**: can reach loopback listeners that forwards and the ssh
  fallback open.
- **upstream's maintainers** *(not in the room)*: have kept mosh's scope narrow for a decade. gosh
  asks nothing of them, carries their name in its notices, and is not to be taken for their work.
- **Anyone who installs a release**: runs code that no person has read in full, reviewed by the
  agent that wrote it, until the third-party review (*Scope decisions*).

## Requirements

First read off the behaviour of `mosh-1.4.0-pf.6` and `remote-mosh` `f85cdd9`, and marked
*(inferred)*; then reviewed and **accepted by the client on 2026-11-13** ("I reviewed the others and
accept them (drop inferred)"), so the marks are gone. The numbers in them are what the code does:
the client accepted them as they stand, and did not choose them.

**Forwarding**

- **FWD-1** *(since v1)* `-L [bind:]port:host:hostport` makes TCP connections to a
  port on the client's machine arrive at `host:hostport` as seen from the server, as `ssh -L`
  does; `-R` the reverse.
- **FWD-2** *(since v1)* `-LU` and `-RU` do the same for UDP datagrams, of any size up
  to 65,507 bytes, without ever delivering a datagram late in place of dropping it.
- **FWD-3** *(since v1)* A forwarded TCP connection survives any loss of contact
  between client and server, for as long as the session does, and nothing at mosh's end times it
  out.
- **FWD-4** *(since v1)* A listener binds loopback unless told another address.
- **FWD-5** *(since v1)* `-N` holds a session and its forwards open with no terminal,
  reads no input, and says `mosh: connected; forwarding.` on standard error once they can be used.

**Getting through**

- **PATH-1** *(since v1)* When the server does not answer by UDP for 3 seconds at the
  start of a session, or 10 seconds later, the session carries on through the ssh fallback without
  the user doing anything, provided ssh can log in again without asking.
- **PATH-2** *(since v1)* A session on the ssh fallback goes back to UDP by itself when
  UDP works again, and closes the ssh connection after a minute unused.
- **PATH-3** *(since v1)* When both ends are behind NATs that keep port numbers, a
  session on the ssh fallback finds a punched path and moves to it; when either end's address
  changes it finds another.
- **PATH-4** *(since v1)* When no punched path can be found and the user has a
  meetup and its key, the session moves from the relay to UDP through the meetup.
- **PATH-5** *(since v1)* A meetup given a key answers nothing, and passes nothing
  on, for anyone who does not show the key.
- **PATH-6** *(since v1)* Two ends on one local network find each other by their
  addresses on it.
- **PATH-7** *(since v1)* No server on the Internet is asked anything unless a session
  has fallen back to ssh, and `--stun=none` or `--punch=never` stops it then too.

**Copying**

- **CP-1** *(since v1)* `mocp [-r] SOURCE... DESTINATION`, one end being
  `[user@]host:path`, copies files as `scp` does, in either direction.
- **CP-2** *(since v1)* A copy that is cut off and run again sends only what had not
  arrived, after checking that what had arrived is the start of the same file.
- **CP-3** *(since v1)* A file appears under its own name at the destination only when
  all of it has arrived and its checksum agrees.
- **CP-4** *(since v1)* A file already at the destination with the same size and
  modification time is not sent.
- **CP-5** *(since v1)* `--auto` then keeps the destination in step with the source,
  one way, looking every 2 seconds; `--delete` also removes what has gone from the source.
- **CP-6** *(since v1)* A receiving end writes only inside its destination, whatever
  names the sending end gives it.
- **CP-7** *(since v1)* `mosh --stdio host -- command` runs the command on the server
  with its standard input and output at the client, well enough to carry `rsync -e`.

**The command, and installing**

- **CMD-1** *(since v1)* `mosh` and `mocp` need `ssh` and nothing else that a bare
  machine lacks: no Perl.
- **CMD-2** *(since v1)* One line installs `mosh`, `mocp`, `mosh-client` and
  `mosh-server` into `~/.local/bin` on Linux (x86-64, ARM64; glibc 2.34 or later) and macOS 11 or
  later, checked against published checksums, without root.
- **CMD-4** *(since v6)* Installing gosh also makes `mosh-client` and `mosh-server` as symbolic
  links to gosh's programs, each only where that name is not already present: "add mosh-* as
  symlinks if not already present" (client, 2026-11-13). So a stock `mosh` command, which asks a
  host for `mosh-server`, finds gosh's where there is no other. *(The agent's reading of "present":
  found anywhere on the installing user's `PATH`, so that a system's own mosh is never shadowed.)*
- **NAME-1** *(since v6)* gosh's commands are `gosh`, `gocp`, `gosh-client` and `gosh-server`; its
  variables begin `GOSH_`; its messages begin `gosh:`; the extension's form is `gosh+HOST`. Wherever
  a requirement here writes `mosh`, `mocp`, `mosh-client`, `mosh-server`, `MOSH_` or `mosh:` for
  something of gosh's own, gosh has the gosh name, and no other alias than CMD-4's. *(client,
  2026-11-13. One requirement and not thirty reissued, so that the names are decided in one place.)*
- **CMD-3** *(since v1)* Either end being an older build is said plainly and at once
  for whatever the older build cannot do, and the terminal works regardless.

**The extension**

- **EXT-1** *(since v1)* Given `user@host`, the extension installs mosh at both ends
  and the editor's server on the host, and opens a window on the host whose connection survives
  what a mosh session survives.
- **EXT-2** *(since v4)* It works with Linux or macOS at either end, and with Windows as the
  user's machine. A Windows machine as the **host is experimental**: it is built,
  shipped and labelled as such wherever it is described and when it is connected to, and nothing is
  promised of it. *(client, 2026-11-13.)*
- **EXT-3** *(since v1)* With `persistentTerminals` set to `shpool`, terminals on the
  host outlive the editor and are found again by the next window.
- **EXT-4** *(since v1)* Folders the user ticks are kept copied to the user's machine
  while a window is connected, one way.

**The class**

- **CLS-1** *(since v5)* *(judged)* In one session, thirty students working together, with the
  student guide and one lab assistant: each student has an AWS server of their own; on it they run
  Python's web server; through the extension they edit an HTML file on that server from their own
  laptop; they see the page rendered on the laptop as they edit; and they can roam between
  networks and lose their connection and still work. Judge: Warren, at that session. *(client,
  2026-11-13. Not said, and so in* Not yet answered*: which laptops; who makes the thirty servers;
  how the page is seen.)*

**Speed**

- **SPEED-1** *(since v6)* A forward in gosh carries bytes end to end at "not more than 10% lower
  overall" than `mosh-1.4.0-pf.7` does on the same machine, by the kept measurement, session by UDP
  and session by the relay. "A specific layer could be a lot slower compared to the old version but
  have no real impact on performance." *(client, 2026-11-13. On 2026-11-13 pf.7 measured 13.1 and
  13.4 Mbit/s by the relay; by UDP 16.7 and 23.1, too unsteady yet to hold anything to.)*

**Upstream**

- ~~**UP-1**~~ *(withdrawn in v6)* Nothing is offered upstream, so no branch is held ready for it.
- **UP-2** *(since v6)* The core in gosh is upstream's at a named commit, byte for byte, and a check
  says so; a fix upstream makes can be taken by moving that commit. *(client, 2026-11-13: agreed to carrying the core
  untouched, and to this wording.)*

## Inputs: absent, late, wrong, lying

*(inferred)* from what the code does today, not from a decision.

| Input | Absent | Late | Wrong | Lying |
| ----- | ------ | ---- | ----- | ----- |
| UDP between client and server | ssh fallback after 3 s / 10 s (PATH-1) | mosh's own handling | datagrams that fail authentication are dropped | not possible without the session key |
| the unattended ssh login the fallback needs | no fallback; mosh sits at "Connecting..." as upstream does; retried at most every 5 minutes | retried, backing off from 1 s to 30 s | same as absent | — |
| a STUN server's answer | path-finding goes on with local addresses and guessed ports | ignored once a path exists | not parsed as an answer: ignored | believed: a wrong address is tried and nothing answers there |
| the other end's build | features it lacks are refused at once with a message (CMD-3) | — | — | a peer that claims a capability it lacks: connections on it time out |
| a source file during a copy | reported, the rest copied, exit status 1 | — | changed while read: not delivered; sent again by `--auto` | — |
| names from the sending end of a copy | — | — | outside the destination: the receiver stops (CP-6) | — |
| a meetup | session stays on the relay | — | — | cannot read or alter what it passes; can drop it |

## Operating envelope

- **Scale:** *(inferred)* one user, one session at a time per use; up to 16 forwards of each kind,
  64 TCP connections and 64 UDP conversations each way per session; a forwarder holds up to 4,096
  pairs. A course of students each with their own sessions has not been tried. Not designed for:
  a shared public forwarder.
- **Speed:** SPEED-1, relative to pf.7. *(measured, not required)* forwarded TCP at 100 ms round
  trip about 9 Mbit/s, at 300 ms about 4, with little delay 20 to 30. No absolute figure is
  required.
- **Recovery:** *(inferred)* client killed: the server's session lasts until its command ends, or
  for a stream session a week. Server machine restarted: the session is gone and the user starts
  another. A copy interrupted in any way is resumed by running it again. Timed: a copy resumed
  after a kill, and a session recovered after an address change (3 s), in `mosh/docs/status.md`.
- **Availability:** none stated.
- **Resources:** *(inferred)* up to 1 MB of buffer each way per forwarded connection.
- **Interruption:** *(inferred)* power or network cut at any instant leaves no file at a copy's
  destination under its real name unless it is whole (CP-3). Tested for the network; not for power.

## Security, law, and ethics

- **Threat model:** *(inferred, and thin)* mosh authenticates and encrypts every datagram with a
  key exchanged over ssh; everything added is reachable only through that, except: loopback
  listeners (forwards, the local relay), the path-finding socket (which accepts only probes sealed
  with a key sent over ssh), and a STUN server or forwarder's port (open to the Internet by
  design). The receiving end of a copy is the part that acts on another machine's word; it is
  fuzzed. **No independent review of any of it has been done**; one is required before gosh is
  called trusted (*Scope decisions*).
- **Data about people:** nothing is stored. Public STUN servers learn the IP addresses of both ends
  of a session that has fallen back to ssh (PATH-7). A forwarder sees which two addresses talk and
  how much. The extension keeps a list of recent host names on the user's machine.
- **Law and ethics:** not examined. Open: whether a course may direct students to software that
  contacts third-party servers by default; whether running a listener on a course server is
  permitted by its operator; licensing of the bundled shpool (Apache-2.0, recorded) and of mosh
  (GPL-3).

## Assumptions (K4s), read back to the client

Read back on 2026-11-13; answers as given.

- "We're assuming students can log in to the course server with an ssh key, or can be taught to in
  one step. True?"
- "We're assuming the course server lets a student run long-lived processes and use UDP ports
  60000 to 61000 or, failing that, repeated ssh logins. True?"
- "We're assuming it is acceptable for a session that has fallen back to ssh to ask Google's and
  Cloudflare's STUN servers where it appears to be, by default. True?" — **True**: "public stun is
  ok" (client, 2026-11-13)
- "We're assuming students on Windows will use WSL. True?" — **True**: "wsl prefer especially
  while the native is experimental" (client, 2026-11-13). *But see* Not yet answered*: the
  extension on a Windows laptop does not run in WSL.*
- "We're assuming nobody but you uses the fork yet, so command and option names may still change.
  True?" — **True**: "it's just us" (client, 2026-11-13)
- "We're assuming a file's size and modification time are enough to decide it need not be copied
  again. True?" — accepted with CP-4 (client, 2026-11-13)

## Scope decisions

- Forwarding is carried beside mosh's state synchronisation, not through it — it would add up to
  250 ms of batching and deliver UDP late instead of never. *(client, 2026-11-12)*
- The TUN back end for TCP was removed — "almost never used, and mostly the same solution".
  *(client, 2026-11-12)*
- UDP forwards are `-LU` and `-RU`, not `-U` — to line up with ssh's `-L` and `-R`. *(client)*
- `--auto` is one way — two-way synchronisation is a different and much harder tool. *(agreed)*
- Parity for fragmented UDP is decided automatically from measured loss. *(client)*
- ~~The working set lives in `mosh-dev`, not in the fork.~~ *(v1 to v5.)* The working set lives in
  `gosh`. `mosh-dev` stays as the record of the fork. *(since v6; client agreed, 2026-11-13)*
- Vocabulary: **meetup** for the public machine; **relay** for the ssh path. Old names
  (`mosh-server stun`, `stun-key`, `MOSH_STUN_KEY`, and `MOSH_STUN` for one's own server) are
  dropped at the next release with no aliases: "drop the old names and I will change my exports.
  it's just us." *(client, 2026-11-13)* `--stun` and `MOSH_STUN` stay, for plain public STUN
  servers.
- Public STUN servers are asked by default. *(client, 2026-11-13)*
- On Windows the documented path is WSL, while the native build is experimental. *(client,
  2026-11-13)*
- `remote-mosh` will be made public, "eventually (soon)". *(client, 2026-11-13; no date)*
- ~~The IPv6 socket fix is to be offered upstream.~~ Not offered: upstream's pull request 1442
  already makes the change. *(since v6)*
- **No new features, for now; a request for one is pushed back on.** What exists is to be read,
  named consistently and made testable before anything is added to it. *(client, 2026-11-13: "no
  new features (push back on that)", on withdrawing `-D`.)* This holds until the client lifts it in
  a new version of this document.
- ~~Requirement IDs are not written in mosh's source.~~ In gosh, code and tests name the
  requirements they serve, by versioned ID. *(since v6; client agreed, 2026-11-13)*
- gosh is a new project, `gosh` and `remote-gosh`, with a history of its own: "make a new gosh and
  remote-gosh project (not a fork or clone) to land it." The code is still mosh's and the fork's, so
  gosh is under GPL-3 and keeps their notices. *(since v6; client, 2026-11-13)*
- The core is carried untouched and pinned, and is not itself migrated into the model. *(since v6;
  client agreed, 2026-11-13)*
- gosh must work with a stock mosh at the other end, and need not with the fork's releases: "it's
  just us". *(since v6; client agreed, 2026-11-13)*
- The fork at pf.7, with `remote-mosh`, is what a class uses until gosh passes the same tests.
  *(since v6; client agreed, 2026-11-13)*
- The parts are named: session (the core), the seam, forwarding, relay, path-finding, meetup, copy,
  the command, install, the extension. *(since v6; client, 2026-11-13. gosh's design may divide one
  further, under the same words.)*
- **Review.** The agent that writes a part also reviews it, and the client accepts that review: "i
  do plan on accepting your reviews of the refactor ... a full review at the end by a third party
  is the only way this would be trusted eventually anyway." So: no person reads each part; the
  tests, the fuzzers and the speed measurement carry the weight meanwhile; and gosh's documentation
  says "not independently reviewed" until a third party has reviewed all of it (SHAPE-4). That review
  comes before the first class uses gosh: "the end is before the first class uses it." The
  client still signs each commit and tags each release. *(since v6; client, 2026-11-13. This
  replaces "read, all of it, by the owner" in the workspace's* Done*.)*
- The model of the old code that the migration works from is kept with viki and its librarian, and
  is scaffolding: "the end goal of this is to not need viki once refactored." What it knew ends up
  in gosh's design and trace. *(since v6; client, 2026-11-13)*

## Non-goals

- Not building two-way file synchronisation, compression, or delta transfer in `mocp` — `rsync -e
  'mosh --stdio'` exists for that.
- Not building a public STUN or forwarding service — a forwarder serves the holder of its key.
- Not supporting a Windows host from the plain `mosh` command — the extension handles Windows
  hosts, experimentally (EXT-2).
- Not building selective acknowledgment for the built-in TCP *(inferred: it was never asked for)*.
- Not offering anything to upstream, and not keeping gosh able to be merged there. *(since v6)*
- Not working with the fork's releases, `mosh-1.4.0-pf.1` to `pf.7`, at the other end. *(since v6)*
- Not migrating the Perl `mosh` script, or changing the fork after pf.7. *(since v6)*

## Not yet answered

- **Who the third party is** that reviews all of it before a class uses gosh. — Warren.
- **The class's laptops.** Which systems will the thirty have? This matters more than it looks:
  on a Windows laptop VSCodium runs as a Windows program, so the extension there uses the *native*
  Windows mosh client, not WSL. That client is the less-tested one (it is installed and started in
  CI, and nothing more). WSL helps the `mosh` command, not the editor. — Warren.
- **Who makes the thirty AWS servers**, and with what firewall rules? A server whose security
  group allows only ssh still works (through the relay), more slowly; one that allows UDP
  60000-61000 works directly. Is there a script or an image, or does each student click through
  the console? — Warren.
- **How is the page "seen rendered locally"?** A browser on the laptop pointed at a forwarded
  port, or a preview inside the editor? The editor forwards ports through its own connection,
  which rides on mosh; that this works here has never been tried. — Warren, after the first trial.
- What is the date of the first class session? Everything in the queue is ordered against it. —
  Warren.
- Is password-only login to be supported, or is an ssh key a precondition? (first assumption
  above) — Warren.
- Does the course server, or AWS, permit what the second assumption says? — Warren.
## Deferred

- Not yet: `-D`, a dynamic forward (a SOCKS proxy on the client's machine, as `ssh -D`) — wanted
  enough that the client thought it was there; not built, and features are frozen. It is two to
  three days' work, not a small one (sketch in the design's *Deferred*). Triggered by the freeze
  being lifted and the client asking again.
- Not yet: IPv6 path-finding (a STUN server and punching over IPv6) — the code is IPv4 only;
  triggered by a student whose home machine has IPv6 behind a firewall they cannot open.
- Not yet: `--exclude` for `mocp` — triggered by the first student who mirrors a project folder
  with its dependencies in it.
- Not yet: local copies for Windows hosts in the extension — `remote-mosh` issue 2.
- Not yet: the extension running inside an editor at all — it has only been run by its tests.
