# Design — v2

The domain as the fork's code shows it, the model gosh migrates into, the map, the seam, speed, and
the order of work.

**Status: accepted by the owner on 2026-11-13** ("all good. continue!", to its questions and
recommendations in section 8, each of which is therefore answered yes). It supersedes
`DESIGN_v1.md`, which is kept: its table of the seam file by file, its account of which tests
drive the programs and which link the fork's classes, and its record of the owner's earlier answers
are still the reference, and where the two differ this document wins (section 7 lists where). One
earlier answer is reversed here: the `Instruction` field numbers move last, not when the seam lands
(question 5). The proposed nouns of 7.2 are in requirements v11.

Written as a recommendation by a second agent (Claude Fable 5.2) that was given the fork's source
and the viki diary and asked to form its own model before reading design v1; the text below is its
own, and still speaks as a recommendation. Nothing here was built or run. Each claim
about the code is marked by how it is known where that matters: **(read)** the text was read at
`mosh-1.4.0-pf.7`; **(inferred)** reasoned from what was read; **(measured)** a figure someone else
measured and wrote down. File names are the fork's at pf.7, under `src/`, with line numbers at that
tag. "The core" is upstream at `decd9b7`.

The model was formed from the source first. `docs/DESIGN_v1.md` was read afterwards; section 7 says
where the two differ. Section 9 says what was read, searched, and not looked at.

---

## 1. The domain as the code shows it

### 1.1 The shape is processes, and half the boundaries are between them

The fork is two programs that each become several processes. This picture is the first thing a
newcomer needs, and no file shows it. **(read)**

```
USER'S MACHINE                                             HOST
gosh (the command)                                         sshd
  ├─ ssh … "SERVER new …"  ──── login, once ─────────────►  └─ gosh-server new   (detaches)
  │        ◄── "MOSH IP", "MOSH CONNECT port key" ───────        ├─ the user's shell, or a sleep loop
  ▼ same process, no exec                                        └─ a stream's process (@copy | @sh),
gosh-client (the client)                                             started when the stream's SYN arrives
  │  ◄═══════ session: sealed UDP datagrams ═══════════►  (gosh-server new)
  │           side channel: 0xFF datagrams beside it
  ├─ a stream's process (@copy | @stdio)   fork, socketpair
  └─ local relay            fork, no exec; UDP on loopback
       ├─ ssh … "SERVER relay ADDR PORT" ── stream ─────►  sshd └─ gosh-server relay  (remote relay)
       │      2-byte length + datagram; control words            │  UDP to the server's own address
       ├─ direct UDP to the server                               │  may outlive its ssh connection 60 s
       ├─ punch socket ◄═ probes, session datagrams ═════════════┘  punch socket
       └─ asker (double fork, 10 s): STUN requests
                         │                     ▲
                         ▼                     │           THIRD MACHINE
                  public STUN server      gosh-server meetup PORT   (answers STUN; passes datagrams on)
```

`gosh-client` is: the command, the client, ssh's ProxyCommand (`--mosh-fake-proxy`), the local
relay, and one end of a stream. `gosh-server` is: the server, the remote relay, the meetup, a key
maker, and the other end of a stream. Which one a process is, is decided by its name and first
words (`mosh-client.cc:126-170`, `mosh-server.cc:212-237`).

### 1.2 The concepts

Each: what it is; the state and its owner; invariants; lifecycle; where it lives today.

**Session.** mosh's connection, with its key. Owner: the core. It is not migrated. What the fork
needs to know of it is four facts and one control: the peer's capabilities, the loss the peer
reports, when the peer was last heard, the largest datagram, and "send to this address from now
on". Today: `network/network.{h,cc}`, `networktransport*.h`.

**Side channel.** Sealed datagrams whose first byte is `0xFF`, sent and received by the session's
own `Connection`, skipping state synchronisation. Never acknowledged or sent again.
- State: one sink pointer, the peer's capability bits, the peer's reported loss. Owner: the
  patched `Transport` (`networktransport.h:68-71`).
- Invariants: nothing is sent until the peer has advertised bit 1, there is a remote address, and
  no shutdown is in progress (`sidechannel_ready`, `networktransport.h:152`). A mosh `Fragment`
  starts with a 64-bit id counting from 0, so its first byte is never `0xFF`. A payload larger
  than `MTU − 12 − 16` is dropped silently (`sidechannel_send`, `networktransport-impl.h:193`).
- Lifecycle: off; enabled by `set_sidechannel_sink` (which also starts advertising); usable when
  the peer's first `Instruction` arrives; unusable again during shutdown.
- A side-channel datagram still counts as "heard from the peer", still moves the server's idea of
  the client's address, and still carries mosh's timestamps (it goes through `Connection::send` and
  `recv_one`). **(read)**

**Capability.** What an end says it can do: two bits in `Instruction.capabilities` (field 8): 1 the
side channel, 2 a stream. Owner: each end states its own; the other end holds a copy, replaced by
every `Instruction` (`networktransport-impl.h:91`). The copy is 0 both when nothing has been heard
and when the peer is a stock mosh; only "an `Instruction` has arrived" tells them apart
(`still_connecting()`). `Instruction.recv_loss` (field 9) rides with it, only when capabilities are
non-zero (`transportsender-impl.h:317`).

**Contact, and what "connected" means.** The code has six answers. **(read)**

| Question | Rule | Where |
| --- | --- | --- |
| has the server ever answered? | a remote state number > 0 | core `still_connecting()` |
| may I send on the side channel? | capability + address + no shutdown | `sidechannel_ready` |
| is the other end in contact? | heard within 10 s (`SIDECHANNEL_QUIET`) | `sidechannel_connected`, `sidechannel.h:63` |
| should the client start the relay? | never heard for 3 s; or silent 10 s | `consider_relay`, `stmclient.cc:360` |
| is UDP answering (in the local relay)? | direct or punched heard within 2.5 s | `from_client`, `relay.cc:617` |
| is the punched path open? | locked, and heard within 3.5 s | `Punch::open`, `punch.h:195` |

`connected; forwarding.` (`REQ1/FWD-5`) is the first two together. The third is a policy of
forwarding's (it stops TCP retries counting while out of contact) but its clock read and its
constant live in the core's transport.

**Forward, and its two ends.** A numbered entry in a list both ends were given. One end *listens*
(a bound socket); the other *connects* to the target. `-L`/`-LU`: the client listens. `-R`/`-RU`:
the server does.
- State: the list (each end, from its own command line); listeners at one end; resolved target
  addresses at the other, resolved **once, when the program starts** (`tcpforward.cc:69`,
  `udpforward.cc:430`); a sequence counter and a 1024-wide replay window per direction and kind.
- Invariants: both ends hold the same list in the same order, because a datagram names its forward
  by index (byte 2 of the frame). Nothing checks this. **(read)** The listening end alone invents
  connection and conversation ids. At most 16 forwards of a kind.
- On the wire: `0xFF`, type, forward, id (2), sequence (8), then data: 13 bytes
  (`UDPForwarder::encode_frame`, `udpforward.cc:293`). Types 0 to 6 name kind and direction.
- Smeared: the frame codec and replay window are members of `UDPForwarder` but serve TCP and the
  stream too (`tcpforward.cc:157,168`). Five forwarder objects are held by `Forwarding`
  (`forwarding.h`), reached through a mux of virtual sinks.

**Forwarded TCP connection.** One TCP connection whose two halves are a real socket at each end
and a TCP of gosh's own between them. Segments travel as side-channel datagrams in TCP-in-UDP
datagram form (12-byte header, `tcpinudp.h`).
- State: `AdapterPort::Conn` (`tcpadapter.h:142-205`): about forty fields, each end holding its
  own half; up to 1 MB of send buffer and 1 MB of receive buffer each. Named in the tunnel by
  (forward, the listening end's port, from 32768 up).
- Invariants: one layer of retransmission. While out of contact, unanswered retransmissions are
  not counted (`timeout`, `tcpadapter.cc:799`); when contact returns everything outstanding is
  sent at once (`tick`, `:932`). A refused target is refused all the way back. At most 64.
- Lifecycle: `CONNECTING` (server's socket reaching the target) or `SYN_SENT` (client) →
  `SYN_RCVD` → `ESTABLISHED` → dead, by both FINs acknowledged and delivered, by reset, or by
  10 unanswered retransmissions while in contact.
- Smeared: the state machine and the system calls are one class. `accept`, `connect`, `recv`,
  `send`, `shutdown`, `poll`, `close` sit between the lines of the TCP logic. The clock is passed
  in on two entries and read from the global on two others (`:464`, `:914`). The initial sequence
  number is drawn from a random source inside `new_conn` (`:271`).

**UDP conversation, and a fragmented datagram.** A conversation is one local sender's exchange
through one UDP forward. Owner: the forwarder at each end (`Flow`, `udpforward.h:250`): at the
listening end the sender's address; at the connecting end a socket of its own. 64 at most, the
quietest evicted; forgotten after 120 s idle. A datagram too large for one side-channel datagram is
cut into equal pieces with a 4-byte header, optionally with one parity piece per 8; the receiver
holds at most 64 partial datagrams for at most 1 s each. Pure already: `ReplayWindow`,
`DatagramAssembly`, `parity_worthwhile`, the frame codec. Not pure: `read_fd`, `deliver`,
`new_flow`, `sidechannel_recv` read the global clock and call sockets.

**Stream, and its two processes.** One reliable byte channel between a process at each end. In
the code it is forward number 0 of a sixth kind (type 6) with no listener and no target
(`tcpforward.h:162-169`): the client hands a socket in (`open_with`), the server makes one when the
SYN arrives (`add_spawner`). What the two processes are is said in *words*, joined by the character
0x1F, carried in `-X` to each program: `@copy …`, `@stdio`, `@sh COMMAND` (`streamchild.h`).
- State: client: the child's pid, exit status and end time (`stmclient.h:87-89`). Server: none;
  its process is double-forked and nobody waits (`streamchild.cc:220`).
- Invariants: the client program's exit status is its own child's (`mosh-client.cc:361-366`). The
  remote process's exit status is never carried. **(read)** A stream session's server gives up
  after a week without its client (`mosh-server.cc:503-510`).
- Lifecycle: session up and side channel ready → client starts its process and opens the stream
  (`stmclient.cc:413-423`) → server starts its process → the client's process ends → shutdown
  when no stream connection is left, or after 5 s (`tend_stream`, `:376`).
- Smeared across: `mosh-client.cc` (`-X`), `stmclient.cc` (start, wait, the "too old" message),
  `mosh-server.cc` (`-X`, the spawner lambda, the timeout, the `pledge` exemption),
  `tcpforward`/`tcpadapter` (forward 0), `streamchild.cc` (the processes), and the command (which
  writes both ends' words).

**Copy, and its resumable file.** One file transfer over a stream: a sender and a receiver
exchanging thirteen kinds of message (`filecopy.cc:136-151`, magic `mocp-1`).
- State: the source file is the authority. The receiver holds a copy in progress as
  `.NAME.mocp-part` beside the target. The sender holds, in memory, what it has offered (`known`,
  `:598`), which is what `--auto` compares each new look against.
- Invariants: a file takes its own name only by `rename`, after size and a 128-bit checksum agree
  (`Receiver::end`, `:1232`; `REQ1/CP-3`). A part is carried on from only if the sender's checksum
  of the same prefix matches (`send_file`, `:828-843`; `REQ1/CP-2`). A name from the sender must
  pass `safe_relative_path` (`:76`; `REQ1/CP-6`). Same size and modification time means "have it"
  (`plan_one`, `:1081-1091`; `REQ1/CP-4`).
- Lifecycle of a file: absent → part → whole and verified → renamed. Of a round: LIST → WANT →
  FILE… → RESULT → ROUND_DONE; with `--auto`, again every 2 s for what changed.
- Shape: two blocking procedures (`Sender::run`, `Receiver::run`) that read the stream, the clock
  (`now_ms`, six places; `time(NULL)` once), `HOME`, the file system and `stderr` from several
  depths. Not on the path of any datagram.

**Path: which way a datagram goes.** The glossary has four paths. The code decides between them
in five places, in three processes. **(read)**

1. The client, once per session: start the local relay and send to it from then on
   (`consider_relay`, `engage_relay`, `stmclient.cc:318-374`). `relay_engaged` is never cleared.
2. The local relay, per datagram from the client: always by direct UDP; by the punched path if
   open; by the ssh connection as well if neither UDP path has answered for 2.5 s
   (`LocalRelay::from_client`, `relay.cc:617-640`). It also opens a fresh direct socket after
   10 s of silence, and closes an ssh connection unused for 60 s (`:803`).
3. The remote relay, per datagram from the server: by the punched path if open; by the ssh
   connection too unless the punched path is open and the ssh connection has been quiet 3 s
   (`relay.cc:423-429`).
4. `Punch`, per probe heard: move to wherever the other end was last heard, except never from a
   straight path to one through a meetup while the straight one is fresh (`punch.cc:591`).
5. The server, per datagram: mosh's own roaming. The first copy of a sequence number to arrive
   sets where it answers (core `Connection::recv_one`). This is what makes "send by every path"
   safe, and it is why the fastest path wins without anyone choosing. **(inferred)**

State: last-heard times per path, in the local relay (`direct_heard`, `Punch::heard`,
`stream_wanted`); the peer's address, in `Punch`. Invariant (`REQ1/SHAPE-2`): there is no state in
which a working path goes unused. After the first fallback the client never again speaks to the
server itself: every datagram, also on the direct path, goes through the local relay on loopback.
**(read)** If the local relay dies, nothing notices: the keep-alive pipe tells the relay the client
has gone, not the reverse. **(read, not run)**

**Relay.** Carrying datagrams inside an ssh connection, by two processes. On the ssh connection:
2-byte length, then the datagram (`relay_frame`, `relay.cc:168`); frames beginning `\0MOSHPCH` are
words between the relays (`HELLO`, `STUN`, `STUNKEY`, `KEY`, `PORT`, `CAND`; `:112-123`).
- State, local relay (`relay.cc:478-505`): the client's address on loopback (it changes); the ssh
  child and its backlog (256 KB at most); a restart delay doubling from 1 s to 30 s, or to 300 s
  while it has never worked; the path times above; a `Punch`.
- State, remote relay (`relay.cc:255-470`): up to four sockets toward the server (the address
  ssh reached, the address given, `127.0.0.1`, `::1`), reduced to the one that answers; a `Punch`;
  whether its ssh connection is still open.
- Lifecycle, remote: started by ssh; if its ssh connection ends while a punched path is open it
  forks, detaches, and stays until 60 s of silence (`:443-470`).
- Settings arrive by environment, read inside the forked local relay (`relay.cc:709-730`):
  `MOSH_PUNCH`, `MOSH_STUN`, `MOSH_MEETUP`, `MOSH_MEETUP_KEY`, a test-only idle time, and a log
  path read on every log line (`:87`).
- The word "stream" in `relay.cc` means the ssh connection (`stream_only`, `STREAM_IDLE_MS`,
  "starting the stream"). The glossary's stream is something else.

**Punched path, candidate, probe.** `Punch` (`punch.cc:397-661`) is one socket carrying three
kinds of datagram: probes (`\0MOSHPCH` + one sealed, numbered byte: `?`, `!`, `.`), STUN answers,
and the session's own datagrams from wherever the other end was last heard.
- State: candidates (16 at most); our own addresses not yet told to the other end; the peer's
  address and whether it is through a meetup; `locked`; `heard`; probe counters. Owner: each relay
  holds one. The key is made by the local relay and sent over ssh (`relay.cc:648`).
- Invariants: only a probe sealed with the shared key, with a number never seen, can say where the
  other end is. A session datagram is taken only from that address.
- Lifecycle: probing every 250 ms for 8 s, then every 2 s; open; kept open by a probe a second;
  closed after 3.5 s unheard; thrown away whenever the ssh connection has to be started again
  (`start_child`, `relay.cc:548`).
- Already close to pure: every entry takes `now` (`test_punch` passes the clock in). Its
  constructor is not: it opens the socket, reads the interfaces, and forks the asker.

**Meetup.** A server loop (`meetup_serve`, `punch.cc:117-273`) and a wire format shared with
`Punch`: the proof of the key (4 random bytes and the 8-byte seal the key gives for them), carried
where a STUN request has its transaction id; and the pass-on header `\0MOSHFWD` + proof (12) +
pair name (16).
- State: pairs (4,096 at most; two ends each; the quieter end replaced; forgotten after 120 s);
  proofs already checked; a count of answers this second (200 at most). Owner: the meetup process,
  in memory.
- It reads the wall clock (`time(NULL)`, twice) and the socket in one loop. It is not fuzzed.
- It lives in the same file as path-finding, and the STUN format is written twice in that file:
  parsed in `stun_parse` (`:275`), and matched and answered by hand in `meetup_serve` (`:231-271`).

**The command, and the words it passes.** `MoshWrapper::run` (`moshwrapper.cc:763-1321`) is one
function of 560 lines that parses options, decides what the session is for, forks ssh, reads the
server's startup message, sets ten environment variables, and returns the client's arguments. The
same process then continues as the client. The words it writes: **(read)**

| To | Form | Content |
| --- | --- | --- |
| the host's shell, by ssh | one shell-quoted line (`:1108`) | `SERVER new -c N [-s\|-i A] [-p P] {-l K=V} {-L\|-R\|-LU\|-RU SPEC} [-F MODE] [-X WORDS] [-- CMD]` |
| itself as the client | argv (`:1294-1305`) | `-# … \|`, the same forward options, `-X WORDS` or `-N`, `IP PORT` |
| itself as the client | environment | `MOSH_KEY` and three prediction and terminal variables (the core's); `MOSH_RELAY`, `MOSH_RELAY_COMMAND` (words joined by 0x1F) |
| the local relay, through the client unread | environment | `MOSH_PUNCH`, `MOSH_STUN`, `MOSH_MEETUP` |
| back from the host | lines | `MOSH IP a`, `MOSH SSH_CONNECTION …`, `MOSH CONNECT port key` |

So on the user's machine a process tells itself what to do by `setenv` and `getenv` and by joining
and splitting strings. The same values are validated up to three times (the relay mode at
`moshwrapper.cc:814` and `stmclient.h:186`; fragment parity at `moshwrapper.cc:807`,
`forwarding.h:126`, and again by each program).

The extension is a second writer of the same words. It starts `mosh-server new -s -L SPEC` and
`mosh-client -N -L SPEC ADDR PORT` itself, sets `MOSH_KEY`, `MOSH_RELAY`, `MOSH_RELAY_COMMAND`,
writes `-X @copy…` by hand, and reads the client's `stderr` for `mosh: connected` and
`through ssh` (`remote-mosh/lib/connect.js`, `lib/mirror.js`, `lib/remote.sh`). **(read)**

**Install.** A release is four names in a tarball (`mosh-client`, `mosh-server`, and `mosh`, `mocp`
as links to the client), a `SHA256SUMS`, and a naming rule `mosh-VERSION-OS-ARCH`. State: the
files in `~/.local/bin`, replaced by rename. The naming rule is written twice: `scripts/install.sh`
and `remote-mosh/lib/install.js:21-30`.

**The extension.** `lib/` (connect, install, mirror; no editor dependence) and `extension.js`
(glue). State: recent hosts, ticked folders.

### 1.3 Findings beside the task

Each was read, not run. None is fixed by this recommendation; they are for `FINDINGS.md` and the
owner (section 8, question 9).

- `gosh --help` would print Perl. The usage text contains `};`, `my $copy_usage =` and `qq{`
  followed by the copy usage a second time: the raw string was not closed where the Perl one was
  (`moshwrapper.cc:170-195`).
- `--client=PATH` is accepted and never read (`moshwrapper.cc:243`; no `value( "client" )`).
- A meetup's proof has no freshness. Any proof once seen is good for ever (`meetup_proof_ok`,
  `punch.cc:107`; the `proofs` cache, `:154`), and it travels in clear. Whoever sees one request can
  use the meetup. This bears on `REQ15/PATH-5`.
- The receiver of a copy checks names (`safe_relative_path`) but opens and renames through whatever
  is already in the destination, a symbolic link included. This bears on `REQ1/CP-6`.
- `tcpadapter.h:76-78` says window scaling is left out; the code does it (`WINDOW_SHIFT`).
- `test_loss_measurement` sleeps 1.05 s five times on the real clock (`tests/udp-forward.cc:855`).
- Requirements v10 still says "forwarder" for the meetup in four places (*Operating envelope*,
  *Threat model*, *Data about people*, *Non-goals*), and the code says "forwarder" for forwarding's
  own objects. One word, two things.

---

## 2. The model recommended

### 2.1 Three rules, the same for every part

1. **A piece is logic or edge, never both, and the file says which.** Logic takes values and
   `now`, and returns values and what to do next. It includes no socket, process, file or clock
   header, calls no `getenv`, and draws no random number. An edge owns descriptors and processes,
   reads the clock once per wake, parses the environment once into an options value, and calls the
   logic. `tools/check.sh` refuses a logic file that includes or names any of those. That turns
   `REQ6/SHAPE-5`'s veto into an exit code.
2. **Every form of words has one definition.** Each part that is told what to do by words owns one
   pure piece, `options`, with both directions: words → value, value → words. The command writes
   with it; the programs and the extension's tests read with it. A round-trip test per part.
3. **No interface with one implementation.** A boundary is a virtual call only where two real
   things stand behind it. Where the second thing is a test recorder, it is a template parameter,
   which the owner named as the remedy and which costs nothing per datagram.

### 2.2 The parts and their pieces

The ten part names are kept. Pieces are named with glossary words, or with words the requirements
already use in prose; the rest are in section 7 as proposed nouns.

#### session (the core)

Untouched. `REQ10/SHAPE-1`, `REQ6/UP-2`. Tests: upstream's suite; `tests/stock-session.py`.

#### the seam

| Piece | Purpose | Kind | From | Serves |
| --- | --- | --- | --- | --- |
| **side channel** | send and receive `0xFF` datagrams; say what the peer can do, what it loses, when it was last heard, how large a datagram may be | patch to the core + one header | `sidechannel.h`; `networktransport*.h`; `transportsender*.h`; `transportfragment.cc`; the `.proto` | `REQ10/SHAPE-1`, `REQ8/CMD-3`, `REQ1/FWD-1..3` |
| **loss** | the share of the peer's datagrams that did not arrive | logic, given sequence numbers and `now` | `Connection::measure_loss`, `network.cc:569-592` | `REQ1/FWD-2` |
| **send elsewhere** | the client sends to another address from now on | patch | `Connection::set_remote_addr`, `network.cc:558` | `REQ1/PATH-1` |
| **headless** | a session with no terminal | patch (guards) | `stmclient.cc:90,217,260,275,399,625,666-680` | `REQ1/FWD-5`; `REQ10/SHAPE-1`'s exception |
| **the programs** | gosh's own `main` in front of the core's, for each program; the loop's four call sites; the "connected" line | gosh code + a small patch | `mosh-client.cc:126-170,194-250,288-300,361-374`; `mosh-server.cc:212-237,303-337,546-567` | `REQ6/NAME-1`, `REQ8/CMD-3`, `REQ1/FWD-5` |

Contracts and failures:
- *Side channel.* `send(payload)`: sent, or dropped silently if not ready or too large; never
  queued. Receiving: a payload goes to forwarding whole, mark included. Absent peer capability:
  nothing is ever sent, and after the first `Instruction` the programs say so (`REQ8/CMD-3`).
  Late: there is no late; a datagram is delivered or lost. Wrong: an unknown type is dropped.
  Lying: a peer that claims a capability it lacks receives datagrams it misreads; connections on
  it time out (requirements, *Inputs*).
- The side channel exposes **facts, not policy**: `last_heard()` replaces
  `sidechannel_connected()`. "In contact" (10 s) is forwarding's rule and moves there, with `now`
  passed in. This removes a clock read and a constant from the patched core.
- *Loss.* `LossMeter::heard(seq, expected_after, now)`, `share()`. The core's patch keeps two
  call lines and a getter. Its test becomes numbers, with no sockets and no sleep.
- Dropped: the non-copyable declaration on `Transport` (not needed for behaviour); the IPv6 socket
  fix (`REQ10/SHAPE-1`, v10); `sidechannel_connected` and `SIDECHANNEL_QUIET` from the core's copy.

Tests today: `forward-e2e` (`-N`), `headless-smoke.py`, `test_loss_measurement`; a stock mosh at
each end (`tests/stock-session.py`). `REQ8/CMD-3`: none.

#### forwarding

| Piece | Purpose | Kind | From | Dropped | Serves |
| --- | --- | --- | --- | --- | --- |
| **forward** (`options`) | parse and write `[bind:]port:host:hostport`; default bind is loopback | logic | `ForwardSpec`, `udpforward.cc:64-102` | the word "UDP" in its error text | `REQ1/FWD-1`, `REQ1/FWD-4` |
| **frame** | the 13-byte header; the replay window | logic | `encode_frame`, `decode_frame`, `ReplayWindow`, `udpforward.cc:104-340` | `encode`/`decode` (wrappers used by tests) | `REQ1/FWD-1`, `REQ1/FWD-2` |
| **fragments** | cut a datagram into pieces with parity; rebuild; decide whether parity is worth it | logic | `DatagramAssembly`, `parity_worthwhile`, `udpforward.cc:139-290,582` | — | `REQ1/FWD-2` |
| **conversations** | UDP: which conversation a datagram belongs to; ids; idle expiry; partial datagrams | logic, given `now` | `Flow`, `Partial`, `new_flow`, `expire_*`, `fragment_recv`, `udpforward.cc:483-540,692-760` | — | `REQ1/FWD-2` |
| **segment** | a TCP segment in datagram form, both ways | logic | `TCPInUDP::from_datagram`, `to_datagram`, `tcpinudp.cc` | `from_tcp`, `to_tcp`, `to_udp`, `checksum`: no caller but tests and a fuzzer since the TUN back end went **(read)** | `REQ1/FWD-1` |
| **connection** | gosh's TCP: one end of a forwarded connection | logic, given `now`, a segment or bytes, and the link's three facts; emits segments and socket actions | `Conn`, `tcpadapter.cc:254-386,416-460,462-845` | `SegmentPort`, `SegmentSink` | `REQ1/FWD-1`, `REQ1/FWD-3` |
| **stream** | forward 0 of the sixth kind; a stream's words; when the client is done | logic | `stream_words*`, `streamchild.cc:51-78`; `tend_stream`'s decision, `stmclient.cc:376-391` | — | `REQ1/CP-7`, `REQ1/CP-1` |
| **listeners and targets** | bind, accept, connect, read, write, close; resolve names once | edge | `add_listener`, `accept_from`, `passive_open`, `flush`, `read_socket`, `tick`'s `poll`, `tcpadapter.cc`; `UDPForwarder` ctor, `read_fd`, `deliver` | — | `REQ1/FWD-4` |
| **a stream's process** | start the process at one end; become `@copy`, `@stdio` or `@sh` | edge | `stream_child`, `become`, `stdio_pump`, `streamchild.cc:80-235` | — | `REQ1/CP-7` |
| **forwarding** (the whole) | own the forwards of each kind; give each arriving payload to its kind; one surface for a program's loop | edge, thin | `forwarding.h` | `SideChannelMux` (a `switch` on five fixed kinds) | all of the above |

*Where the pieces stand, since 2026-11-14 (the owner: "forwarding seems a little too pooled";
"subdirectories i agree"): `src/forwarding/` holds `forward`, `frame` and the whole;
`src/forwarding/tcp/` holds connection, segment, and TCP's listeners and targets;
`src/forwarding/udp/` holds fragments, conversations, and UDP's listeners and targets;
`src/forwarding/stream/` holds the stream and a stream's process. Subdirectories and not
parts: forwarding stays the one part the glossary names. The three include nothing of each other.*

*Two departures from the table above, accepted by the owner on 2026-11-14 (queue item 16):*

- *`SegmentSink` is kept, where the table has it dropped. The table's reason was that it is an
  interface with one implementation; it has two: `TCPForwarder`, and the recorder in the fork's
  unit test `tcpforward-test.cc` (the fork's `tcp-forward.cc`), which is how that test sees every segment the adapter sends. To
  drop it would be to rewrite a carried test that holds the code to what it did at pf.7.*
- *`TCPForwarder` is not divided into logic and edge. It resolves names and reads the clock, and
  what it decides is about twenty lines: a frame around each segment, the replay window, a
  check that a segment fits, and which of the two ports the frame carries. Those read only what
  the session says and no clock. A piece of their own would add a file and a template for
  twenty lines; if they grow, that is the time.*

The **connection** piece is the centre of the part and of the speed question. Its shape, as a
sketch and not a contract:

```
struct Link { bool ready; bool in_contact; size_t max_datagram; };   // read by the edge, per call

template <class Out>          // Out: send(forward, segment), connect(id), deliver(id),
class Connections {           //      shut_write(id), close(id, reset)
  void opened   (forward, id, now, Link, Out&);            // a listener accepted, or a stream was opened
  void segment  (forward, const Segment&, now, Link, Out&);
  void reached  (id, bool ok, now, Link, Out&);            // the target answered, or refused
  void read     (id, const char*, size_t, now, Link, Out&);// bytes from the socket; 0 length is its end
  void wrote    (id, size_t n);                            // the socket took n of what was waiting
  void failed   (id, Out&);
  void tick     (now, Link, Out&);
  int  wait_time(now) const;
};
```

`Out` is the edge in the programs and a recorder in tests, which is what `RecordingSink` is today.
The initial sequence number is passed in by the edge.

Contracts and failures (forwarding as its neighbours see it):
- To the seam: gives payloads to `send`; is given payloads. Reads `ready`, `last_heard`,
  `max_payload`, `loss`.
- To a program's loop: descriptors to watch, `read(fd, now)`, `tick(now)`, `wait_time(now)`.
- Absent: no side channel at the peer → a new TCP connection is reset at once (`accept_from`,
  `tcpadapter.cc:395`); UDP datagrams are dropped. Out of contact → connections wait, for as long
  as it takes (`REQ1/FWD-3`). Target does not answer → 10 s, then reset back to the application.
- Late: a UDP datagram is never delivered late: a partial older than 1 s is abandoned
  (`REQ1/FWD-2`). TCP segments out of order are held, up to 1 MB.
- Wrong: a frame for an unknown forward, a malformed segment, a fragment that contradicts its
  datagram: dropped. An MSS below 64 is not honoured.
- Lying: a replayed datagram is refused by the replay window; one older than 1,024 behind is
  refused unseen. A reset is believed only within 65,535 of where the other end should be. A
  mismatched forward list is not detected (1.2).

Tests today: (u) `tcp-forward` (`test_tiny_mss`, `test_outage`, `test_transfers`, `test_remote`),
`tcp-in-udp`, `udp-forward` (`test_spec`, `test_wire`, `test_replay`, `test_fragments`,
`test_relay`, `test_remote`); fuzzers `forward_frame`, `tcp_adapter`, `udp_forward`, `tcpinudp`;
(b) `forward-e2e`, `mocp.test` (`--stdio`), `headless-smoke.py` (`-X`). With logic apart from
sockets, `test_outage`'s six hours and the adapter fuzzer's corpus run with no socket at all.

#### relay

| Piece | Purpose | Kind | From | Serves |
| --- | --- | --- | --- | --- |
| **options** | the relay's settings as one value, written by the command, parsed once | logic | `RelayOptions`; the five `getenv`s, `relay.cc:709-730`; `MOSH_RELAY`, `MOSH_RELAY_COMMAND`, `mosh-client.cc:288-300` | `REQ1/PATH-1`, `REQ1/PATH-7` |
| **frame** | 2-byte length framing; the words between relays | logic | `relay_frame`, `RelayFrameParser`, `control`, `is_control`, `relay.cc:103-190` | `REQ1/PATH-1` |
| **start** | should the client start the relay now? | logic: `(now, started_at, last_heard, mode, already, shutting_down) → bool` | `consider_relay`, `stmclient.cc:360-374` | `REQ1/PATH-1` |
| **paths** | given when each path was last heard: which to send this datagram on; start, restart or close the ssh connection; open a fresh direct socket; and, at the remote relay, which to answer by and when to leave | logic, plain numbers | `from_client` `:617-640`; the idle close `:803`; `start_child`'s delays `:551-553`; the reply rule `:423-429`; the linger `:443-470` | `REQ1/PATH-2`, `REQ1/SHAPE-2` |
| **local relay** | the process on the user's machine | edge | `LocalRelay`, `relay_start_local`, `relay.cc:478-876`; `engage_relay`, `stmclient.cc:318-357` | `REQ1/PATH-1`, `REQ1/PATH-2` |
| **remote relay** | `gosh-server relay ADDRESS PORT` | edge | `relay_remote`, `relay.cc:255-470` | `REQ1/PATH-1` |

Contracts and failures:
- To the session: UDP datagrams only, on loopback at the client and at the server's own address.
  It never opens one.
- To path-finding: `paths` is told `punched_heard` and `punched_open` as numbers; the edges call
  path-finding and carry its words.
- Absent: ssh cannot log in unattended → retried, 1 s doubling to 300 s; the session sits at
  "Connecting…" (requirements, *Inputs*). The local relay itself absent → not handled at pf.7
  (1.2); report, do not choose a behaviour here.
- Late: datagrams by ssh arrive late and not never; a backlog over 256 KB is dropped.
- Wrong: a frame that is not a known word is passed on as a datagram, and mosh drops it.
- Lying: the relays cannot read or forge what they carry.
- Dropped: `MOSH_RELAY_STREAM_IDLE_MS` as an environment name (the idle time becomes a field of
  the options value that a test sets); the log path read per line (read once).

Tests today: (b) `relay.test` → `headless-smoke.py` (`always`, `blocked`, `translated`,
`punched`); `wrapper.test` (`--relay=always`); the `returned` case on the fork's branch
`refactor/relay`; (u) `test_relay_frames`. `paths` and `start` get the unit test queue item 7 asks
for: every change of path, with numbers.

#### path-finding

| Piece | Purpose | Kind | From | Serves |
| --- | --- | --- | --- | --- |
| **STUN** (asking) | the request; reading an answer; `a.b.c.d:p` as text | logic | `stun_request`, `stun_parse`, `parse_ip_port`, `format_ip_port`, `punch.cc:72-86,275-341` | `REQ1/PATH-3` |
| **punch** | candidates; probes sealed and numbered; where the other end is; when to probe; through a meetup as second best | logic: given `now`, and a datagram with where it came from; returns datagrams to send, a session datagram, goodbye, or new addresses of ours | `Punch::add_candidate`, `tick`, `receive`, `send`, `goodbye`, `punch.cc:477-661` | `REQ1/PATH-3`, `REQ15/PATH-4` (the ends' side), `REQ1/PATH-6` |
| **socket, interfaces, asker** | the punch socket; this machine's addresses; the forked process that looks names up and sends the requests | edge | `Punch::Punch`, `ask_stun`, `punch.cc:345-465` | `REQ1/PATH-6`, `REQ1/PATH-7` |

Contracts and failures:
- Absent: no STUN answer → local addresses and the other end's port at the address ssh knows are
  tried (`relay.cc:374,664`). No path found → the session stays on the relay.
- Late: an answer after a path exists adds a candidate and changes nothing.
- Wrong: what does not parse as an answer, a probe that does not open, a probe number already
  heard: ignored.
- Lying: a STUN server's wrong answer is believed, tried, and nothing answers there. A stranger
  cannot move the path: only a sealed, fresh probe does.
- An empty list of servers asks nobody (`ask_stun`, `:347`). Whether the list is empty is relay's
  `options` (`REQ1/PATH-7`).
- To add when it lands (queue item 8, `../mosh-dev/docs/FINDINGS.md`): a change from one path to a
  better one is returned as a value, so the edge can log it and a test can wait for it.

*Since 2026-11-15 path-finding is switched on in gosh, its meetup half replaced by tickets
(`registering`, below), and a probe opened only at its one length; and divided as above: **STUN**
is `stun.h`, **punch** and **registering** are `PathFinding` in `finding.h` (logic: it returns the
datagrams to send), and the edge is `Punch` in `punch.*`. Since requirements v16 it is IPv4 and
IPv6 (`REQ16/PATH-8`): one socket for both where the system allows, IPv6 first.*

| **registering** | the meetup: where it sees us, REGISTER under the ticket, again every 25 s, asking again after three unanswered; PASS and PASSED | logic, in `PathFinding` (`use_meetup`, `tend_meetup`, `from_meetup`); its bytes are meetup's `messages` | written for gosh | `REQ15/PATH-4`, `REQ15/PATH-5` |

Tests today: (u) `stun-test`; `finding-test` (no socket, no clock); `punch-test` (straight;
through a meetup; a forged ticket; an oversized probe);
(b) `headless-smoke.py ... punched`, `relay-meetup.py`. Not yet against gosh: `nat-lab.py`.

#### meetup

*Since 2026-11-15 the meetup is built to `PROPOSAL_meetup-registration.md`, revision 10: the
student's own server, used with a ticket given over ssh. The fork's shared key and copyable proof
are gone from it, and from path-finding. Since requirements v16 it listens by IPv4 and IPv6
(`REQ16/PATH-8`), and "where" carries either.*

| Piece | Purpose | Kind | From | Serves |
| --- | --- | --- | --- | --- |
| **ticket** | a ticket's id, its key from the master key, when it stops being good, its text | logic | written for gosh | `REQ15/PATH-5` |
| **messages** | REGISTER, REGISTERED, PASS, PASSED at exact lengths; sealing and opening under a ticket's key | logic | written for gosh | `REQ15/PATH-5` |
| **registrations** | who is where for each ticket; which counters are used; whom a datagram goes to; forgetting; room for 64 | logic, given `now` in seconds | written for gosh; the fork's `Pair` table (`punch.cc:139-222`) was its place | `REQ15/PATH-4`, `REQ15/PATH-5` |
| **answer** | recognising a STUN request and writing the answer; the rate limit | logic | `punch.cc:231-271`; `PER_SECOND`, `:159,237-244` | `REQ15/PATH-4` |
| **master** | the master key's file in gosh's directory | edge | written for gosh | `REQ15/PATH-5` |
| **serve** | `gosh-server meetup PORT`: the socket and the loop | edge | `meetup_serve`, `punch.cc:117-138,160-170` | `REQ15/PATH-4` |

Contracts and failures: without a ticket the meetup made, nothing but STUN is answered and
nothing passed on (`REQ15/PATH-5`). A REGISTER is taken only from the address sealed in it, and any
sealed message only with a counter not used, so a copied datagram moves nothing and is passed no
second time. Absent other end → the datagram is dropped. Lying: it cannot read or alter what it
passes; it can drop it. One test writes an answer with meetup's `answer` and reads it with
path-finding's `STUN`: the format is in two parts and this is what keeps them one format.

Tests today: (u) `ticket-test`, `messages-test`, `registrations-test`, `answer-test`; (b)
`tests/meetup-tickets.py`, sealing with Python's AES-OCB, a second implementation.

#### copy

| Piece | Purpose | Kind | From | Serves |
| --- | --- | --- | --- | --- |
| **options** | `send`/`receive` and their flags, both ways | logic | `FileCopy::parse`, `filecopy.cc:99-131` | `REQ1/CP-1`, `REQ1/CP-5` |
| **messages** | the thirteen messages: writing and reading | logic | `put*`, `Reader`, the `MessageType`s, `:136-257` | `REQ1/CP-1` |
| **names** | is a name safe; where it lands; its part file's name | logic | `safe_relative_path` `:76`, `target_of` `:1013`, `part_name` `:1030`, `top_name` `:709` | `REQ1/CP-6`, `REQ1/CP-3` |
| **checksum** | 128 bits, not cryptographic | logic | `Hash`, `:63` | `REQ1/CP-2`, `REQ1/CP-3` |
| **decisions** | receiver: have it, or want it from which offset; sender: is the part the start of this file; what changed since last offered; is what arrived whole | logic, given what `stat` said | the middles of `plan_one` `:1081-1115`, `send_file` `:828-843`, `scan` `:727-750`, `end` `:1247-1262` | `REQ1/CP-2`, `REQ1/CP-3`, `REQ1/CP-4`, `REQ1/CP-5` |
| **sender, receiver** | the two procedures: files, the stream, the clock for `--auto`, what the user is told | edge | `Channel`, `End`, `Sender`, `Receiver`, `:259-1367` | `REQ1/CP-1`, `REQ1/CP-5` |

How far to go, which DESIGN_v1 leaves open: **keep the two blocking procedures.** They are the
edge. Copy is on no datagram's path, and a blocking procedure over a stream is the simple version.
Move out only what a test needs without a file system: the five logic pieces above. `HOME` is
read once and passed in; the clock is passed to `Channel` and to the `--auto` wait.

Contracts and failures: as requirements, *Inputs* (a source file absent, changed while read; names
outside the destination). The other end absent → `Closed`; exit status 1; run again to resume.
Wrong → `Malformed`; stop.

Tests today: (u) `file-copy`, `file_copy_fuzzer`; (b) `mocp.test`, the `-X @copy` checks of
`headless-smoke.py`.

*Since 2026-11-16 copy is divided as the table says (queue item 9): `options.h`, `messages.h`,
`names.h`, `checksum.h` and `decisions.h`, each with its test, and `filecopy.cc` the two
procedures, with `HOME` read once in `run` and the clock passed in. Every format is pf.7's; the
fork's `file-copy` is `filecopy-test.cc`, now the edge's test. The fork's `file_copy_fuzzer` was
not carried into gosh. `src/copy/README.md`.*

#### the command

| Piece | Purpose | Kind | From | Serves |
| --- | --- | --- | --- | --- |
| **options** | the option table; words → settings; what is refused | logic | `OPTIONS`, `find_option`, `parse_options`, `check_port_request`, `moshwrapper.cc:233-460`; the validations `:800-870` | `REQ1/CMD-1` |
| **what was asked** | settings → the server's words, the client's options, both ends of a stream, the relay's command; which end of a copy is which | logic | `run`, `:869-1000,1067-1108,1255-1306`; `shell_words`, `shell_quote`, `remote_place` | `REQ1/CP-1`, `REQ1/CP-5`, `REQ1/CP-7`, `REQ1/PATH-1`, `REQ1/PATH-7`, `REQ8/CMD-3` |
| **startup message** | read `MOSH IP`, `MOSH SSH_CONNECTION`, `MOSH CONNECT`, and which server answered | logic, line by line | `:1179-1254` | `REQ8/CMD-3` |
| **login** | run ssh, or a local shell; ask the terminal's colours | edge | `:1002-1042,1110-1178` | `REQ1/CMD-1` |
| **proxy** | ssh's ProxyCommand: reach the host, say which address, carry bytes | edge | `fake_proxy`, `resolve`, `self_path`, `proxy_command`, `:462-575,673-761` | `REQ1/CMD-1` |

It hands the client a **value**, in the same process. No `setenv`, no join and split. The textual
forms remain for the two places text must travel: the line ssh carries to the host, and whatever
starts `gosh-client` from outside (the extension, a stock `mosh` script through `REQ6/CMD-4`'s
link). Failures: ssh absent or refused → said, exit 1. No startup message → "Have you installed
…". The server is not gosh's → said once; no forward, stream or relay asked of it (`REQ8/CMD-3`).
Dropped: the Perl in the usage text; the second copy of the copy usage; `--client`, unless the
owner wants it kept as accepted-and-ignored (question 9).

Tests today: (b) `wrapper.test`, its built-in half. The three logic pieces get unit tests with
plain strings: none exists today beyond `shell_words`, `shell_quote`, `remote_place`.
*(Done 2026-11-16: `src/command/` is now `options`, `words` (what was asked), `startup`, `login`
and `proxy`, the three logic pieces each with its test; `src/command/README.md`. The variables
for the client stay variables: the core's client and the relays read them.)*

#### install

`install.sh` (with `REQ6/CMD-4`'s links, each only where the name is not on `PATH`),
`package-release.sh` (refuses a program that needs a library the system does not ship), the release
workflow. `REQ1/CMD-2`, `REQ6/CMD-4`, `REQ1/SHAPE-3`, `REQ6/NAME-1`. Scripts are edge; there is no
logic piece worth separating. What it lacks is a test: `install.sh` already takes where releases
come from (`MOSH_RELEASES`) and where to put them (`PREFIX`), so a test can install a locally
packaged release into a temporary home and check the links both ways. Fails: wrong checksum,
a client that does not run, no `curl` or `wget`: each refused with a message **(read)**.

#### the extension

In `../remote-gosh`. `REQ1/EXT-1`, `REQ4/EXT-2`, `REQ1/EXT-3`, `REQ1/EXT-4`. Its `lib/` is already
apart from the editor. Logic to name: the report-line reader (`MOSHREMOTE key==value==`), the
script builders, the relay command. What the model asks of it: it writes gosh's words only as
gosh's `options` pieces define them, and one test in gosh, which the extension's CI also runs,
checks the forms it uses. Tests today: `test/connect.test.js`, `test/shpool.test.py`.

---

## 3. The map

### 3.1 Who may call whom (within a process)

| Caller | May call |
| --- | --- |
| the core | nothing of gosh's, except the seam's listed call sites |
| the seam: the programs | the command; forwarding; relay (`options`, `start`, the local relay's start; the remote relay); meetup (`serve`); the core |
| forwarding | the seam: side channel. From `a stream's process` only: copy (`options`, and to become one end) |
| relay | path-finding |
| path-finding | meetup: `ticket` and `messages` only |
| meetup, copy | nothing of gosh's |
| the command | each part's `options` piece, to write words. Nothing else |
| any part | the core's crypto, `Addr`, byte order. Not its clock, not `Connection`, not `Transport` |

Logic pieces call only logic pieces. Edges call their own part's logic and the edges listed above.

### 3.2 What crosses a process or a machine

| From → to | Carrier | What |
| --- | --- | --- |
| the command → the host's shell | ssh, one line | the server's words (1.2) |
| the host → the command | ssh, lines | `MOSH IP`, `MOSH SSH_CONNECTION`, `MOSH CONNECT port key` |
| the command → the client | the same process | a value |
| the extension, or a stock `mosh` → the client | argv, environment | the client's words; `MOSH_KEY` |
| client ↔ server | UDP, sealed by the session | mosh's `Instruction`s, with capabilities and loss; side-channel payloads: frame (13) + segment (12 + options + data), or datagram, or fragment (4 + piece) |
| client → local relay | fork: memory; a pipe whose closing means "the client has gone" | relay's options value |
| client ↔ local relay; remote relay ↔ server | UDP on the machine | the session's datagrams, untouched |
| local relay ↔ remote relay | ssh, a byte stream | length (2) + datagram; `\0MOSHPCH` + words |
| relay ↔ relay, punched | UDP | `\0MOSHPCH` + sealed probe; session datagrams, untouched |
| an end ↔ a meetup | UDP | a STUN request and its answer; since 2026-11-15, REGISTER, REGISTERED, PASS and PASSED under a ticket (`src/meetup/messages.h`; the fork's `\0MOSHFWD` + proof is gone) |
| an end → a public STUN server | UDP | a STUN request |
| a program → a stream's process | fork; a socket pair | the stream's words; then bytes |
| copy ↔ copy | the stream | `mocp-1` messages: type (1), length (4), body |
| the extension → the host | ssh, a script | settings; back: `MOSHREMOTE key==value==` |
| the client → the extension | `stderr` | `mosh: connected`, `through ssh`, `mocp: …` lines |
| install ← a release | https | `NAME-VERSION-OS-ARCH.tar.gz`, `SHA256SUMS` |

### 3.3 One-way doors

**Not gosh's to change** (a stock mosh speaks them): the session's datagrams and the existing
`Instruction` fields; `MOSH CONNECT`, `MOSH IP`; `MOSH_KEY` and the prediction and terminal
variables; the core's own options of `mosh-client` and `mosh-server`; `MOSH_SERVER_NETWORK_TMOUT`.
When gosh's programs are run under mosh's names (`REQ6/CMD-4`) they must take exactly these.

**Gosh's, free today, shut at its first release** ("it's just us"; no working with the fork's
releases): the two `Instruction` field numbers and the capability bits; the frame and its seven
types; the segment form and the window-scale option; the fragment header; relay's framing and
words; the mark `\0MOSHPCH`; the probe; the meetup's four marks, its messages and its ticket's text; `mocp-1` and
`.NAME.mocp-part`; the stream's words and 0x1F; the relay's and path-finding's variables; release
names; the `stderr` lines the extension reads; the report lines.

**Recommended to change, and nothing else:**
1. The `Instruction` field numbers (decided: DESIGN_v1, question 2). **But last, not first**
   (question 5). *Done 2026-11-16: `capabilities` is field **1001** and `recv_loss` field **1002**
   (seam patches 01 and 02), clear of upstream's 1 to 7 and in the range left by custom for
   extensions; each costs one byte more per datagram than 8 and 9 did. A one-way door from gosh's
   first release that carries it. From then a gosh end and a pf.7 end take each other for stock.*
2. Names `REQ6/NAME-1` requires: `GOSH_` variables, `gosh:` messages, with the extension in step.
   *Done 2026-11-16, in the same change: `GOSH_RELAY`, `GOSH_RELAY_COMMAND`, `GOSH_RELAY_LOG`,
   `GOSH_RELAY_STREAM_IDLE_MS`, `GOSH_PUNCH`, `GOSH_STUN`, `GOSH_MEETUP`,
   `GOSH_MEETUP_TICKET_COMMAND`; mosh's own (`MOSH_KEY` and the rest above) keep their names.*
3. Relay's words `STUN` and `STUNKEY`, which still carry the dropped vocabulary
   (`relay.cc:113-115`), become meetup's words. *Done with the tickets (2026-11-15): `STUNKEY` went;
   `MEETUP` and `TICKET` came; `STUN` names plain STUN servers again, the glossary's word since v15.*

Every other format stays byte for byte what pf.7 writes. Not because it is good, but because then
pf.7 is a second implementation to test against while parts are reshaped (section 6), and because
no requirement asks for a change.

**User-facing names the requirements already fix:** `-L -R -LU -RU -N --stdio --copy --relay
--punch --stun --meetup --auto --delete`, `gosh-server relay|meetup|meetup-key`.

---

## 4. The seam

### 4.1 What the core must expose

| # | Change to the core | Where it came from (at `decd9b7`) | Why | Hook or refactor |
| - | --- | --- | --- | --- |
| 1 | two optional fields in `Instruction`; the sender puts them in; a change in either makes a new instruction; the receiver keeps them | `transportinstruction.proto`; `TransportSender::send_in_fragments`, `transportsender-impl.h:316`; `Fragmenter::make_fragments`, `transportfragment.cc:160`; `Transport::recv`, `networktransport-impl.h:77` | a stock peer must never be sent a `0xFF` datagram (`REQ10/SHAPE-1`); a peer must be told what will not work (`REQ8/CMD-3`) | additions |
| 2 | a first-byte test in `recv`; `sidechannel_send`; `ready`, `max_payload`, `loss`, `last_heard`, `peer_can` | `Transport::recv`, `networktransport-impl.h:68`; `networktransport.h` | forwarding (`REQ1/FWD-1..3`). It must be inside `recv`: `connection` is private, and `Fragment( s )` would be built from the payload otherwise | additions |
| 3 | two lines in `recv_one` calling a loss meter; a getter | `Connection::recv_one`, `network.cc:482-487` | parity follows measured loss (`REQ1/FWD-2`). The sequence number is a local there | addition; the arithmetic lives in gosh |
| 4 | `get_last_heard` | `Connection`, `network.h` | relay's start (`REQ1/PATH-1`); forwarding's "in contact" (`REQ1/FWD-3`) | addition |
| 5 | `set_remote_addr` | `Connection`, beside `hop_port`, `network.cc:116` | the client sends to the local relay (`REQ1/PATH-1`) | addition |
| 6 | headless: no terminal read, drawn or restored; a fixed window size; `Display( false )` | `STMClient::init`, `shutdown`, `main_init`, `output_new_frame`, `main`, `stmclient.cc:82,204,236,275,433` | `REQ1/FWD-5`; `REQ10/SHAPE-1`'s exception | guards |
| 7 | four call sites in each program's loop: least wait, descriptors to watch, what became readable, once per pass | `STMClient::main`, `stmclient.cc:454,468,486,553`; `serve`, `mosh-server.cc` near `723,747,858` | forwarding, relay's start, a stream's end, all need a turn in the loop | additions |
| 8 | one call after the session's transport exists | `STMClient::main_init`, `stmclient.cc:270`; `run_server`, `mosh-server.cc:439` | to give forwarding the side channel | addition |
| 9 | the exit line and status when headless or for a stream; `pledge`'s conditions | `main`, `mosh-client.cc:215`; `stmclient.cc:441`; `mosh-server.cc:644` | `stdout` is not the client's to write on when it carries a stream | small changes |

### 4.2 gosh's own `main` in front of the core's

The fork changes 162 lines of `mosh-client.cc` and 157 of `mosh-server.cc`, mostly to parse options
and dispatch on names and first words. None of that needs to be inside the core's `main`. The fork
already does it in front: `mosh-client.cc:126-170` rewrites `argc` and `argv` and falls through.

Recommended: each program's `main` is gosh's. It dispatches the name (`gosh`, `gocp`,
`--mosh-fake-proxy`) or the first word (`relay`, `meetup`, `meetup-key`); takes gosh's options out
of the arguments into one value; then calls the core's `main`, compiled under another name. The
core's `main` then sees only arguments a stock mosh would give it. The hooks of rows 6 to 8 read
that one value.

What this buys: the two program patches fall from about 580 added lines to the guards and call
sites of rows 6 to 9, which is roughly 60 to 80 lines **(inferred, not counted on a real patch)**.
Option words, names and the environment never enter a patch, so moving the pin cannot break them.

What it costs, said plainly: one process-wide value, set once before the core's `main` runs. It
rests on "one session per process", which is true of both programs **(read)**. And gosh's `main`
must not disturb `getopt`'s state for the core's `main`; it should not use `getopt` at all.

### 4.3 Where a refactor of the core is warranted

**Nowhere, for this model.** Every row above is an addition or a guard. That is a finding, not a
default: each was checked against the text. Three candidates, with what would make each warranted:

- *The loops read one datagram per pass* (`STMClient::main`: "we only read one socket each run";
  `serve`, `mosh-server.cc:763`). Draining would be a reshaping of the core's loop, from
  `stmclient.cc:475-489`. Warranted only if a profile shows the pass itself is what limits a
  forward, and then it makes gosh *faster* than pf.7, which `REQ14/SPEED-1` does not ask.
- *`STMClient::shutdown` and `init`* hold terminal set-up and tear-down inline. The fork guards
  `shutdown` by re-indenting twelve lines (`stmclient.cc:215-232`). Extracting them would shrink
  the headless patch. Warranted when a moved pin first conflicts there; until then, a guard that
  does not re-indent.
- *`Connection::send` copies the payload three times* (`new_packet`, `toMessage`, `encrypt`).
  Warranted only by a profile.

Rejected: a headless client loop of gosh's own, with no `STMClient`. It would need no headless
patch, but it would copy the core's shutdown and timeout handling (`stmclient.cc:500-560`), which
is the session's, and stop receiving upstream's fixes to it.

### 4.4 Agreed with DESIGN_v1, briefly

The core as a submodule; the seam as an ordered series of patches against a copy; each patch
headed by its pin, its reason, and for a refactor its origin. All agreed. The series as DESIGN_v1
lists it holds, with "one per program" becoming much smaller by 4.2.

---

## 5. Speed

`REQ14/SPEED-1` is relative: not more than 10% under pf.7, session by UDP and by the relay.

**Measured, by others:** pf.7 on this machine, on loopback: 13.1 and 13.4 Mbit/s by the relay; 16.7
and 23.1 by UDP, "too unsteady yet to hold anything to" (requirements, 2026-11-13).

**Everything below is read or inferred. Nothing was run.**

### 5.1 What one forwarded TCP segment costs today **(read)**

*Sending* (bytes from an application, at the listening end), per segment of about 1.2 KB:
- one `sendto`; the `recv` from the application is shared by up to 50 segments;
- two virtual calls: `SegmentSink::to_network`, `SideChannelLink::sidechannel_send`;
- about seven copies and six allocations: `send_buf.substr` (`tcpadapter.cc:754`); into the
  `TCPInUDP` (`:339`); `to_datagram`; `encode_frame`; then in the core `new_packet`, `toMessage`,
  `encrypt`.

*Receiving*, per segment:
- four system calls: the loop's `select`, `recvmsg`, `send` to the application, and `sendto` for
  the acknowledgment (every data segment is acknowledged at once; `tcpadapter.cc:554-559`);
- three virtual calls: the mux, the forwarder, the adapter;
- about five copies: the core's decrypt and payload; `decode_frame`; `from_datagram`; into
  `recv_buf`.

*Per pass of the loop*, and a pass is made for every datagram, because the core reads one per
pass:
- `Forwarding::fds()` builds up to six vectors; `read_fd` is offered to all five forwarders, each
  searching its lists; `tick` and `wait_time` each walk every connection;
- the core's own `tick` and `wait_time` each run `calculate_timers`, which compares states.

*Per acknowledgment*, at the sender: `send_buf.erase( 0, acked )` (`tcpadapter.cc:657`) moves
whatever is left of a buffer of up to 1 MB. With a window that grows to 1 MB this is, per
acknowledgment, the largest memory operation on the path. **(inferred to matter; not measured)**

*By the relay*, added per datagram each way: the local relay's `poll`, `recvfrom`, a copy, a
frame, a `write`; ssh; the remote relay's `read`, `RelayFrameParser::next`, which erases from the
front of its buffer per datagram (`relay.cc:188`), and a `send` to each socket still open. One
datagram per `poll` wake on the UDP side (`relay.cc:764`).

### 5.2 What follows

1. **The boundaries are not the cost.** Five virtual calls per segment stand beside five system
   calls, a dozen copies, an AES-OCB seal and a pass of two programs' loops. **(inferred)** This
   agrees with DESIGN_v1's "virtual calls stay until a profile points at one" on speed. I would
   still remove three of the four interfaces, for rule 3 of 2.1, not for speed.
2. **The unsteady UDP figure is probably not a CPU figure.** The window may reach 1 MB
   (`SEND_BUFFER`), a loopback UDP socket's buffer is smaller than that by default, and the
   receiver takes one datagram per pass. So on loopback the sender can overrun the receiver's
   socket, and what is measured is then loss and recovery, which varies run to run. **(inferred)**
   `../mosh-dev/docs/FINDINGS.md` records the same mechanism on a real path ("Large windows made
   forwarded TCP slower"). If so, a 10% gate on that figure passes and fails by chance.
3. **So measure before bending anything.** Two things, both small, both in queue item 4:
   a profile of pf.7's client and server under `tools/speed.py` (where the time goes, by
   function); and a second, steadier figure beside Mbit/s: processor seconds per megabyte carried,
   client and server each. The first turns this section from inferred to measured. The second
   gives the gate something that does not move by chance.

### 5.3 Where the model bends for speed

These cost nothing in comprehension and are chosen now, because changing them later changes every
signature on the path:

- **Logic emits through a template parameter, not a returned list.** `Connections<Out>` calls
  `out.send( … )` as it goes. No vector of segments per call, no virtual call, and the logic still
  knows no socket.
- **`now` is a parameter.** The core's `timestamp()` is already a value frozen once per `select`
  (`util/select.h:182`), so passing it costs nothing and loses nothing.
- **One virtual boundary stays: the side channel's `send`.** The client's and the server's
  transports are two different types, so forwarding either is a template over the transport or
  calls through one pointer. One indirect call beside a `sendto` and a seal is the boring choice.
- **Reading a frame and a segment gives a view into the datagram, not a copy.** Two of the five
  receive copies go, at the two decode functions, where a pointer and a length are the natural
  result anyway.

These wait for the profile: the send buffer as something that does not move on every
acknowledgment; fewer copies on the send path; draining the socket (4.3). Each is an improvement
over pf.7 rather than parity, and each is easier once `connection` is logic that a unit test
drives with numbers.

One thing must **not** be bent: the per-datagram path gets no objects with identity, lookup
services or event queues. A connection is found by (forward, port) in a list of at most 64, as
today.

---

## 6. Order of work

### 6.1 What I would change in the queue

| Queue | Recommendation | Why |
| --- | --- | --- |
| 4 | add: a profile of pf.7 under `speed.py`, and processor seconds per megabyte as a second figure | 5.2 |
| 5 | split: **5a** the seam, the thin command, and the fork's TCP forwarding *carried unchanged*; **5b** TCP forwarding reshaped | so a change in speed or behaviour has one cause; and 5a leaves the old code in the tree to test the new against |
| 5 | bring a **thin command** into 5a | item 5's *done* already needs it: "`gosh -L` to a stock server says once … and asks the server for none" is the command's work (`moshwrapper.cc:1065-1108`), and the queue has no `gosh` until item 10 |
| 5 | **do not renumber the `Instruction` fields in item 5**; do it as the last change before the first release | question 5 |
| 6 | split: **6a** the stream; **6b** `-R`, and UDP forwards | copy needs the stream, not the rest. The stream is small once `connection` exists: it is forward 0 |
| 7, 8 | keep the order. Split **meetup** out of 8: it is a process of its own with no caller but `gosh-server meetup`, and can land at any time | `meetup_serve` calls nothing of relay's or `Punch`'s **(read)** |
| 9 | "blocked on 5" should read "blocked on 6a" | `mocp.test` and `-X` go through the stream |
| 10 | the rest of the command (copy's and relay's words, the proxy) and install | the thin command went into 5a |
| 11 | as is; `REQ1/EXT-4` additionally waits on 9 | `lib/mirror.js` writes `@copy` words |

### 6.2 The steps, with what is risky and what would show it wrong early

**Step 1 (5a): a thin vertical slice.** `gosh -N -L PORT:HOST:PORT host`, from the command line to
a byte arriving at the target, every layer present and thin: the command (login, which server,
the startup message, `REQ8/CMD-3`'s notice); gosh's `main`; the transport patches; headless; the
loop's call sites; the fork's `tcpforward`, `tcpadapter`, `tcpinudp` and the frame, **moved and
not changed**. It is also exactly the path the extension uses (`connect.js`: `-N -L`), so the slice
carries the mission's load (`REQ5/MISSION-1`).
- Risky: gosh's `main` in front of the core's; the patch series applying in order; running under
  mosh's names for a stock `mosh` script.
- Shows it wrong: `tests/stock-session.py` both ways; `forward-e2e` (`-L`, `-N`) green against
  pf.7 and then gosh; a gosh client against a pf.7 server and the reverse, which works only while
  the wire is unchanged; the speed figures, which must not move at all, since the forwarding code
  is the same text. If they move, the seam costs something and that is known before anything else
  is built on it.

**Step 2 (5b): reshape TCP forwarding.** `frame`, `segment`, `connection` as logic;
`listeners and targets` as edge.
- Risky: speed; and TCP behaviour changing in ways no test names.
- Shows it wrong: a differential test, cheap because step 1 left the old class in the tree: the
  same recorded sequence of segments, socket events and times goes into `AdapterPort` and into
  `Connections`, and what each emits is compared. The adapter fuzzer's corpus is the first set of
  recordings. Then `test_outage` with no socket, the speed figures, and gosh against pf.7. The old
  class is deleted when the part lands.

**Step 3 (6a): the stream.** Forward 0, the words, the process at each end, the client's "done"
decision. Risky: process lifetimes and signals (`../mosh-dev/docs/FINDINGS.md`, "could not be told
to stop"). Shows it wrong: `mocp.test`'s `--stdio` case; `headless-smoke.py`'s `-X` checks.

**Step 4 (9): copy.** Risky: the least of the parts if the procedures are kept (2.2); the most if
they are rewritten. Shows it wrong: `mocp.test`; the fuzzer with the `climb` seed; a copy from a
gosh sender to a pf.7 receiver and the reverse.

**Step 5 (7): relay**, with path-finding switched off, which the code already allows: every use of
`Punch` in `relay.cc` is behind `if ( punch )`, and `--punch=never` leaves it null **(read)**.
Risky: `paths` must reproduce timings no test pins (2.5 s, 3 s, 10 s, 60 s, the back-off). Shows
it wrong: the unit test of every path change with numbers, written against the fork's rules
before the code moves; `relay.test` (`always`, `blocked`, `translated`); the `returned` case.

**Step 6 (8): path-finding**, then **meetup** if not already landed. Risky: the `room` case fails
at random at pf.7, cause unknown. Shows it wrong: `punch` returning "moved to a better path" as a
value, the lab waiting for it, twenty passes in a row (queue item 8's own *done*).

**Step 7 (6b): `-R` and UDP forwards.** Mostly logic already. Shows it wrong: `forward-e2e`; the
frame and UDP fuzzers.

**Step 8 (10): the rest of the command; install.** Do the `REQ6/CMD-4` trial first (a stock client,
a bare host), as the queue says. **Then renumber the `Instruction` fields.**

**Step 9 (11): the extension.**

The order differs from the queue in one substantive way: the stream and copy come before relay.
Reason: they finish the forwarding model (a third user of `connection`) while it is fresh, and
copy is the part whose shape is least settled. If the owner would rather have the fallback early,
steps 5 and 6 can go before 3 and 4 with nothing else changing.

---

## 7. Where this differs from DESIGN_v1, and proposed nouns

### 7.1 Differences, each with its evidence

1. **A stream's process belongs to forwarding, not to copy.** DESIGN_v1: copy is "one end of a
   copy over a stream; and the process at each end of any stream" (`filecopy.*`, `streamchild.*`).
   That is two purposes. `streamchild.cc` starts three kinds of process (`become`, `:143-198`):
   `@copy`, `@stdio`, `@sh`. Only the first is a copy. `REQ1/CP-7` (`--stdio`) is carried by the
   other two and touches no line of `filecopy.cc`. The glossary gives streams to forwarding
   ("the part that carries forwards and streams"). The one arrow is stream → copy
   (`stream_words_valid` calls `FileCopy::parse`, `:74`; `become` calls `FileCopy::run`, `:184`),
   so copy stays free of `fork`, `exec` and the shell.

2. **"Which path" is decided in five places, not two.** DESIGN_v1's fourth decision names
   starting the relay and choosing the ssh connection per datagram. The code adds the remote
   relay's reply rule (`relay.cc:423-429`), `Punch`'s rule for leaving a meetup path
   (`punch.cc:591`), and the server's own roaming, which is what makes the rest safe. The first
   two of these must be in the `paths` and `punch` logic and in queue item 7's unit test, or the
   test drives half the decision. The `room` finding is about the fourth.

3. **The programs' patches can be much smaller than "call sites", by a `main` of gosh's own**
   (4.2). DESIGN_v1 keeps option parsing as a patch that shrinks. The fork's own
   `mosh-client.cc:126-170` shows the arguments can be rewritten before the core's parsing runs.

4. **No refactor of the core's loops is needed.** DESIGN_v1: "The first such are the programs'
   loops, where a hook has to be made." Read line by line, the fork's changes to both loops are
   four insertions each and move nothing (`stmclient.cc` hunks at 452, 465, 487, 550;
   `mosh-server.cc` at 723, 747, 858). They stay additions (4.3).

5. **Three of the four interfaces go, and not for speed.** DESIGN_v1: "Virtual calls stay until a
   profile points at one." `SegmentPort` has one implementation, `AdapterPort`
   (`tcpadapter.h:84`); the second went with the TUN back end. `SegmentSink` has `TCPForwarder`
   and two test recorders. `SideChannelSink` is crossed twice to reach one of five fixed members.
   Once the TCP logic is apart from sockets, as `REQ6/SHAPE-5` asks, the first two have nothing to
   stand between. This is the "interface with one implementation" the working agreement forbids.

6. **"In contact" should leave the seam.** DESIGN_v1 lists it among what the transport offers and
   notes it is "one more clock read, behind the seam". The patched `Transport` has both
   `last_heard()` and `sidechannel_connected()` (`networktransport.h:169-176`): one fact and one
   policy over it. The seam keeps the fact.

7. **The extension does not run `gosh` and `gocp`.** DESIGN_v1's map: "the extension │ runs gosh,
   gocp". At `9ff0118` it starts `mosh-client` and `mosh-server` itself with the low-level words
   (`connect.js`: `spawn( client, ['-N', '-L', …] )`; `remote.sh`: `mosh-server new -s -L`;
   `mirror.js`: `-X` with `@copy` words written by hand). If the map states an intention, it is a
   change to the extension that no queue item holds (question 8). If it describes today, the arrow
   goes to the programs, and the words are a contract with a second writer.

8. **The command may call the parts' `options` pieces.** DESIGN_v1: the command "calls none of
   them" and meets them "only as words", with a test that what it writes is what they parse. One
   definition with both directions is smaller than two definitions and a test, and the fork
   already validates the same word in up to three places (1.2). The arrow is to pure code only.

9. **The first picture should be the processes.** DESIGN_v1's map is of calls, with the processes
   in notes under it. All but one of the sixteen boundaries in 3.2 are between processes or
   machines.

10. **Copy: how far.** DESIGN_v1 defers it. Recommended: keep the procedures, extract five logic
    pieces (2.2).

11. **Order.** A thin command in the first slice; carry before reshaping; the renumbering last;
    the stream before the rest of forwarding (6.1).

Agreed, without more: the submodule; patches as an ordered series; a directory per part with a
check on includes; mosh's own names staying mosh's; relay owning its decisions as functions of
numbers; what is not carried; the two things the carried code does not do.

### 7.2 Proposed nouns

Not assumed. Each is the owner's to accept, rename or refuse. Words already used in the
requirements' prose are marked.

| Noun | Meaning | Today |
| --- | --- | --- |
| capability | what an end says it can do, in each `Instruction` | in *Inputs* ("claims a capability"); not in the glossary |
| contact; in contact | the other end was heard within 10 s | `REQ1/FWD-3` says "loss of contact" |
| listener; target | the two ends of a forward: the bound socket; where the other end connects | `REQ1/FWD-4` says "listener"; code says `LISTEN`, `CONNECT`, target |
| connection | a forwarded TCP connection | *Operating envelope* |
| conversation | one local sender's exchange through a UDP forward | *Operating envelope*; code says "flow" |
| frame; segment; fragment; parity | forwarding's wire words | "parity" is in *Scope decisions* |
| local relay; remote relay | the two helper processes, each by name | the glossary says "the two helper processes" |
| probe; candidate | path-finding's sealed "are you there"; an address the other end might be at | *Threat model* says "probes" |
| pair; proof | the two ends a meetup passes between; what shows its key | *Operating envelope* says "pairs" |
| words | the textual form of what a program is asked to do | DESIGN_v1; the fork's comments |
| startup message | the lines a server prints for the command | the fork's own error text |

Two words to retire: **forwarder** (the requirements use it for a meetup, the code for
forwarding's objects) and relay's **stream** (the ssh connection; `REQ1/PATH-2` already says "the
ssh connection").

---

## 8. Questions for the owner

Each is his: vocabulary, a one-way door, or scope. Each has a recommendation.

1. **May each program's `main` be gosh's, with the core's called from it** (4.2)? It adds one
   process-wide value. *Recommended: yes.* It is what keeps the seam small when the pin moves.

2. **Is a stream's process forwarding's, and copy only the transfer** (7.1, 1)? Then `REQ1/CP-7`
   is served by forwarding and the command, under its present ID. *Recommended: yes.*

3. **The glossary says the side channel is "the seam's one interface".** By the code the seam
   also gives relay two things (last heard; send elsewhere), gives the programs a turn in the
   loop, and holds headless. *Recommended:* the row says "the seam's interface to forwarding" in
   the next version.

4. **The nouns of 7.2**, and retiring "forwarder" and relay's "stream". *Recommended: accept
   them.* Most are already the requirements' own words.

5. **When do the `Instruction` field numbers move?** Queue item 5 moves them first. While every
   wire format is byte for byte pf.7's, a gosh end can be tested against a pf.7 end, part by part:
   a second implementation, for nothing. That catches a mistake made the same way in gosh's writer
   and reader, which gosh against gosh cannot. It is scaffolding, and does not touch the non-goal
   "not working with the fork's releases". *Recommended:* move them last, before the first
   release, in one change with `GOSH_` and the relay's words.

6. **Copy keeps its two blocking procedures** (2.2), with five logic pieces taken out.
   *Recommended: yes.* The judge of `REQ6/SHAPE-5` would be asked to pass a part whose edge is
   large; that is the trade to accept or refuse.

7. **The speed gate.** By UDP the figure is unsteady, and section 5 infers why. *Recommended:*
   before item 5 lands, a profile of pf.7, and processor seconds per megabyte as a second kept
   figure. `REQ14/SPEED-1` says "by the kept measurement"; which measurement is kept is yours.

8. **The extension and the command.** It repeats the command's steps and the release naming.
   *Recommended:* local copies (`mirror.js`) use `gocp --auto`, whose reason for not being used,
   Perl, is gone since pf.5. Connecting keeps its own single login, because it installs and starts
   the editor's server in that same login and must learn its port before the forward is named; but
   it writes only words that gosh's `options` pieces define. **(inferred from reading; not tried)**

9. **The findings of 1.3.** A proof with no freshness; a destination's symbolic links; a forward
   list that is not checked between the ends; a local relay whose death nobody notices. Each
   bears on a requirement and none is a migration task. *Recommended:* into `FINDINGS.md` now, to
   the third party's attention, and no change of behaviour while features are frozen. Two are
   plain defects to fix as their part lands: the Perl in the usage text; `--client` accepted and
   ignored (drop it, or say it does nothing).

10. **The patch for headless** guards five functions of `STMClient`. *Recommended:* guards that do
    not re-indent, and extraction only when a moved pin first conflicts (4.3). Named here because
    a refactor of the core is the holder's to warrant.

---

## 9. What this rests on

**Read in full, at `mosh-1.4.0-pf.7`:** `network/sidechannel.h`, `forwarding.h`, `udpforward.{h,cc}`,
`tcpforward.{h,cc}`, `tcpadapter.{h,cc}`, `tcpinudp.h`, `relay.{h,cc}`, `punch.{h,cc}`,
`streamchild.{h,cc}`, `filecopy.{h,cc}`, `frontend/moshwrapper.{h,cc}`; the whole diff from
`decd9b7` of `network.{h,cc}`, `networktransport{,-impl}.h`, `transportsender{,-impl}.h`,
`transportfragment.cc`, `transportinstruction.proto`, `stmclient.{h,cc}`, `mosh-client.cc`,
`mosh-server.cc`, `frontend/Makefile.am`, `network/Makefile.am`, `configure.ac`;
`scripts/install.sh`, `scripts/package-release.sh`. In gosh: `REQUIREMENTS_v10.md`, `DESIGN_v1.md`
(after the model was formed), `QUEUE.md`, `FINDINGS.md`, `CONTRIBUTING.md`, `README.md`. In
`mosh-dev`: `docs/FINDINGS.md`, `docs/DESIGN_v1.md`, `docs/TRACE.md`.

**Read in part:** the core's `network.cc` (send, receive, port hopping), `util/timestamp.cc`,
`util/select.h`, `transportsender-impl.h` (`calculate_timers`), `stmclient.cc` and `mosh-server.cc`
(the loops); `remote-mosh/lib/connect.js` (most), `lib/remote.sh` (its last third);
`tests/tcp-forward.cc` (`test_outage`), `tests/udp-forward.cc` (`test_loss_measurement`, the start
of `test_punch`); `tools/speed.py` (its first 60 lines).

**Searched or skimmed only:** `tcpinudp.cc` (its function list); the other unit tests (function
names); the fuzzers (their header comments); the shell and Python tests (their header comments);
`release.yml` (step names); `remote-mosh/lib/mirror.js`, `lib/install.js`, `extension.js`
(by pattern). Line numbers were taken by search at the tag. The viki diary was asked one question
by meaning; it worked, and added nothing to what had been read.

**Inferred, and marked so where it matters:** every statement in section 5 about what limits
speed; that the server's roaming is what selects the fastest path; that the forwarders' sockets
being close-on-exec makes the server's `forwarder.reset()` in its child redundant; the size of the
program patches under 4.2; that the extension could use `gocp`; the four findings of 1.3 that bear
on requirements.

**Not looked at:** the fork's man pages and `docs/` (`design.md`, `status.md`, `usage.md`,
`students.md`); `scripts/mosh.pl`; the fork's `ci.yml`; `forward_fuzz.h` and the corpora; the
bodies of `forward-e2e.py`, `headless-smoke.py`, `nat-lab.py`, `fake-ssh.py`; `remote.ps1`,
`terminal.sh`, shpool, the extension's tests; the core's terminal, state synchronisation and crypto
beyond their interfaces; gosh's `tools/` and `tests/` bodies; requirements v1 to v9;
`../mosh-dev/HANDOFF.md`; the viki outlines themselves.

**Not done:** nothing was built, run, profiled or measured. No patch was cut, so "applies in
order" and every line count for the new seam are estimates. The model has not been tried against
Windows (Cygwin, MSYS2), where `fork` without `exec` and descriptor inheritance behave differently
and where three of the fork's open problems live.
