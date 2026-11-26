# Proposal: the domain map, ABI and inside

*A draft for the owner to review, 2026-11-26. Asked for: "shape the domains into abi/imp. ... what
is important is their meaning and that they isolate"; "that can be a linter job". Decided by the
owner on the first draft: "STUN is a domain; move the end into meetup. i thought of filecopy and
moshcommand as two applications in a transport harness: rx/tx x sh/cp"; "sh is the terminal
session; yes, split command"; "connect is different - it is connection aware"; "use session and
copy, and draft v21". Nothing is proved by this, and no code is changed by it. Read from
`strong/main` at 7986be7.*

Each domain is a part of gosh, named by the glossary's word for what it does. Its **ABI** is the
headers another domain may use, and what they mean; everything else in it is its **inside**, which
only the domain's own code uses. An ABI may be several headers, where each has a meaning of its own.

## The shape: a harness, and applications at its ends

gosh is a **transport harness** that carries **applications** between two ends. The harness logs
in, starts the far end, finds and keeps a way to it, and carries what is sent; it knows no
application. An application knows nothing of how it is carried. There are two, each with two ends:

| | **tx**: the end that sends what the application is about | **rx**: the end that takes it |
| --- | --- | --- |
| **session**, the terminal session | the client, on the user's machine: keys out, the screen in *(the agent's reading: the terminal's "what it is about" is what the user types)* | the server, on the host: the shell's screen out |
| **copy**, a file copy | the sender: says what it has, and sends what is lacked | the receiver: says what it lacks, and takes a file only whole |

Either end of a copy may be the local one; a session's tx, its client, is always local. The
terminal itself is the core's (mosh's, SHAPE-1): what is the session's here is gosh's code around
it. `gosh` starts a session and `gocp` a copy: the commands keep their names. The names are the
glossary's *session* and *copy*; the new nouns, *application* and *harness*, and *ABI* and
*inside*, are proposed in `REQUIREMENTS_v21.md`.

## The map

**The harness**

| Domain | Directory | What it is | ABI | Inside |
| --- | --- | --- | --- | --- |
| **command** | `src/command/` | log in by ssh, start the far end's server, and run an application between the two ends | `moshwrapper.h`: `run`, given which application and its words; `fake_proxy`, ssh's ProxyCommand | `login`, `proxy`, `sshconfig`, `startup`, `stock`, `options` and `words` less what moves to sh and cp (below) |
| **relay** | `src/relay/` | the session carried over ssh when UDP cannot get there | `relay.h`: the remote relay, and the local one started. `start.h`: when a relay is started, and again | `frame`, `local`, `remote`, `paths`, `process`, `settings`, `whose` |
| **pathfinding** | `src/pathfinding/` | a way straight between the two relays when both ends are behind NATs | `punch.h`: `Punch`, one end's path-finding: candidates in, a meetup used through meetup's end, ticked by the clock | `finding.h`, `punch.cc` |
| **stun** *(new)* | `src/stun/` | the STUN question and its answer, as bytes, from anyone (RFC 5389's Binding) | `stun.h`: a request made; whether bytes are a request; an answer read; an answer made | today's `pathfinding/stun.h`, and `src/strong/stun/` (today's `strong/pathfinding/`: `StunAnswer`, `is_stun_request_checked`) |
| **meetup** | `src/meetup/` | the machine where the two ends of a ticket learn where they appear to be, and through which their datagrams pass | **the meetup**: `meetup.h` (`meetup_serve`), `master.h` (the master key). **a ticket**: `ticket.h`. **an end of a ticket** *(new)*: `end.h`, `MeetupEnd`: registers from where it is, keeps the meetup's time, seals and opens a PASS; what `Finding` does of it today | `messages.h` (the wire format, now inside), `registrations.h`, `answer.h` (by stun's ABI), `sealed.h`, `serve.cc`, `master.cc`; and `src/strong/meetup/` |
| **forwarding** | `src/forwarding/` | TCP and UDP forwards and the stream, over the side channel | `forwarding.h`: `Forwarding`, every forward of a session, as the side channel's sink. `stream/stream.h`: when the stream is over. `stream/streamchild.h`: the process at an end of the stream | `forward`, `frame`, `fuzz.h`; `tcp/`, `udp/` (each uses `forward` and `frame`, and nothing of the other); and, from `seam/applications.cc`, the stream's ends that carry bytes and mean nothing: `@stdio` (this process's input and output), `@sh` (a command, by the user's shell) and `@connect` (a TCP connection, made by each address in turn, joined both ways, each way ending on its own: a forward whose far side is the stream, as ssh's `-W`) |
| **seam** | `src/seam/` | every place gosh meets the core, and where the harness and the applications are put together | for the core's patches: `core-main.h`, `hooks.h`. For tests: `fuzz-replay.h`, `proved.h` | `client-main.cc`, `server-main.cc`, `session`, `streaming`, `relaying`, `applications`, `options`: it uses every domain's ABI, and nothing uses it but the core and tests |

**The applications**

| Domain | Directory | What it is | ABI | Inside |
| --- | --- | --- | --- | --- |
| **session** *(new)* | `src/session/` | the terminal session: gosh's code around the core's client (tx) and server (rx) | `session.h`: the words and variables each end is started with, from what was asked | from `command/words.*`: `client_words`, `client_variables`, the terminal's part of `server_words` (colours, locales); from `command/login.*`: asking the terminal its colours; from `command/options.*`: the prediction and terminal settings |
| **copy** | `src/copy/` | a file copy: sender (tx) and receiver (rx), over the stream | `filecopy.h`: `run( options, in, out )`, one end. *(new)* `ends.h`: from what was asked, which end is the sender and what each is told | `checksum`, `decisions`, `messages`, `names`, `options`; from `command/words.*`: the copy's part of `what_was_asked`, `remote_place` |

**Shared, and not gosh's**

| | Directory | What it is | ABI |
| --- | --- | --- | --- |
| **strong** | `src/strong/` | what every proved class is written with | `datagram.h`, `opened.h`; and the trusted three, `bounded.h`, `caller.h`, `subarray.h` (strong-domain-driven-design's `prover/trusted/`) |
| the core | `src/core` (`src/crypto/`, `src/network/`, `src/util/`, `src/include/`) | mosh, left alone (SHAPE-1) | any of it, as it stands |

A domain's proved classes, under `src/strong/<domain>/`, are its inside: they stand there because
what is proved stands under `src/strong` (the prover's rule), not because they are a domain apart.

## How it isolates

The lint holds it, from Clang's reading of all of gosh (the prover's `lint.py` reads every file
already), by four rules:

1. A name declared in a domain's inside is used only by that domain.
2. An ABI header's declarations name only ABI types, the standard library's, the core's, and numbers.
3. Nothing writable is shared between domains.
4. No domain keeps a pointer or reference into another's state.

Tests are held to it too: a test that uses another domain's inside is a test of that domain, and
goes there. Nothing of this costs anything when the program runs.

## What changes, by the decisions above

| | Change | From |
| - | ------ | ---- |
| 1 | **stun** made: `pathfinding/stun.h` and `strong/pathfinding/` move to `src/stun/` and `src/strong/stun/`; meetup's `answer.h` and pathfinding use `stun.h` | "STUN is a domain" |
| 2 | **meetup's end**: `MeetupEnd` in `meetup/end.h`, made of what `Finding` does of registration; `messages.h` becomes meetup's inside; pathfinding knows no wire format (it uses 20 of `messages.h`'s names today, 30 with its tests) | "move the end into meetup" |
| 3 | **command split**: the terminal's words to `src/session/`, the copy's to `src/copy/ends.h`; `run` is given the application | "split command" |
| 4 | relay's tests that use `pathfinding/finding.h` move to pathfinding's | the lint |
| 5 | pathfinding's tests that run a meetup from its inside (`registrations.h`, `answer.h`) go through `meetup.h`, or to a test of both under `tests/` | the lint |

## The map's form, for the lint, and what it found

The map is `src/DOMAINS`: a line per domain, its name, its directories, `|`, its ABI headers; and
an `allow FILE DOMAIN` line for each crossing that is there and is to go. The lint
(strong-domain-driven-design's rule *a domain*) says every allowed crossing on every run, refuses
any other, and refuses an `allow` whose crossing is no longer made, so the list only shrinks. It
is the map of gosh as it stands, before the changes above: the applications' and stun's lines come
with their changes.

Run on gosh as it stands (`strong/main`, a33503c), the rule found what this proposal had not:

- **Headers that are boundary, and were not in the draft's ABI:** copy's `options.h` (`run` takes
  its `Options`, and the seam makes one); forwarding's `udp/rules.h` (`Forwarding` takes a
  `ParityMode`); seam's `gosh-version.h` (the command prints it). They are ABI in the map.
- **A crossing the draft had not seen:** relay uses pathfinding's `PUNCH_MARK` (`process.h`,
  `frame-fuzzer.cc`) and an address's text, `parse_ip_port` and `format_ip_port`, from
  pathfinding's `stun.h` (`process.h`, `local.cc`). *Proposed:* `PUNCH_MARK` is on punch's
  boundary, in `punch.h`; an address's text goes with stun, or to a header of its own that both use.
- **pathfinding's `punch.h` names `Got`,** declared in `finding.h`: its boundary leaks its inside.
- **The rule itself was wrong once:** it read a local in an inline function's body (seam's
  `hooks.h`) as part of the boundary. Fixed there, with a case.

78 crossings are allowed today, by 12 lines.

## Not decided here

- **`@sh COMMAND`**, a command run by the user's shell with the stream as its input and output:
  no longer a clash, the applications being session and copy, so its word stays. It means nothing
  to gosh above the bytes, as ssh's `ssh host command`, so it is forwarding's, beside `@connect`
  and `@stdio` (the owner, 2026-11-26: "connect is different - it is connection aware").
- **Where the rule lives:** strong-domain-driven-design's lint, as a rule any program can take,
  with the map as gosh's. Its red cases: each crossing in 4 and 5, seen refused.
- **The order.** The map and the READMEs first. Then the rule, with 4 and 5 fixed. Then 1, 2 and 3,
  each its own change, the whole check passing after each, nothing in the core touched.
