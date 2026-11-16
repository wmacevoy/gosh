# Using gosh

What gosh gives you, how it compares with mosh and with shpool, and how to set it up today. For your own server on AWS, by IPv6, and the
meetup on it, see [`AWS.md`](AWS.md). What gosh must do is `docs/REQUIREMENTS_v16.md` in the
repository. This page says what works and what does not; the requirements are the authority.
(The `REQn/ID` marks below name a requirement there, and the tests that check it.)

**Not independently reviewed.** No third party has reviewed gosh yet, and one is required before a
class uses it (requirements, *Scope decisions*). Until then, treat it as a tool its author tests,
not as one anyone has vetted.

## What you get

gosh runs [mosh](https://mosh.org)'s own session, untouched, and adds what mosh leaves out
beside it.

| | mosh 1.4 | gosh |
| --- | --- | --- |
| A terminal that survives roaming, sleep and lost connections, with local echo on slow links | yes | yes: the same code (`core/`, upstream's at a pinned commit) |
| Reaching the server by UDP, IPv4 or IPv6 | yes | yes |
| Port forwards: TCP `-L`/`-R` and UDP `-LU`/`-RU`, surviving loss of contact | no | yes (`REQ1/FWD-1` to `FWD-3`) |
| A session with no terminal, just forwards (`-N`) | no | yes (`REQ1/FWD-5`) |
| A command's input and output across the session (`--stdio`, e.g. for `rsync -e`) | no | yes (`REQ1/CP-7`) |
| File copies that resume where they were cut off (`gocp`, like `scp`) | no | yes (`REQ1/CP-1` to `CP-6`) |
| When UDP is blocked: the session carried over ssh, and back to UDP when it can be | no: "Connecting..." forever | yes, by itself (`REQ1/PATH-1`, `PATH-2`) |
| Both ends behind NATs: a way straight through found while on ssh | no | yes (`REQ1/PATH-3`) |
| Neither way works: datagrams passed through your own server (a *meetup*) | no | yes, with a ticket fetched over ssh (`REQ15/PATH-4`, `PATH-5`) |
| All of the above by IPv6, IPv6 first when both work | direct UDP only | yes (`REQ16/PATH-8`) |
| The command needs Perl | yes | no: it is C++ (`REQ1/CMD-1`) |
| Works with a stock mosh at the other end | — | the terminal does; gosh says what it cannot do there (`REQ8/CMD-3`) |

What stays the same as mosh: the screen is redrawn, not streamed, so your terminal's own
scrollback does not hold the session's output (use tmux or your editor); and the session lives
in the server's memory, so it does not survive the server machine restarting.

**The paths, best first** (glossary, *path*): direct UDP; a punched path through NATs; through
the meetup; through the relay over ssh. gosh tries them all and keeps trying the better ones; you
choose nothing (`REQ1/SHAPE-2`).

### Compared with shpool

[shpool](https://github.com/shell-pool/shpool) and gosh both keep your shell running on the
server when the connection drops; they differ in what sits between you and it. *(What is said
here of shpool is from its published description, not checked against its source.)*

- **shpool** keeps the shell under a daemon on the server, and you reach it by ssh, over TCP.
  When ssh drops, you run ssh again and `shpool attach` the named session. It passes output
  straight through, so your terminal's scrollback works. It has no network of its own: no
  roaming, no local echo, nothing when ssh cannot get through.
- **gosh** (mosh's core) has its own encrypted UDP protocol. The client roams to a new address
  without reconnecting; the server keeps the screen and sends the latest of it, not a backlog; you
  see your typing at once on a slow link. ssh only starts the session, or carries it when UDP
  cannot.

They fit together: the editor extension (`REQ1/EXT-3`) uses shpool to keep terminals on the host
across editor windows, while gosh carries the connection.

## What is not done yet

Said as plainly as what is (`REQ1/SHAPE-4`):

- **No one-line install** (`REQ1/CMD-2`, queue item 10). You build from source, below.
- **Messages and variables still say `mosh`** in places (`mosh:`, `MOSH_RELAY`, the usage text):
  the change to gosh's names (`REQ6/NAME-1`) is queue item 10.
- **The editor extension** (`remote-gosh`) is not migrated yet (queue item 11); `remote-mosh`
  with the fork is what a class uses meanwhile.
- **Not tried on real networks by the tests:** two machines behind real NATs, a meetup between two
  machines, and IPv6 between two machines. Every path is tested on one machine, by IPv4 and by
  IPv6 loopback (`tools/check.sh`). See `src/pathfinding/README.md`, *Not tested*.
- **macOS and BSD** have not run the relay or path-finding. Windows is through WSL.

## Setting it up

You need gosh on **your machine** (the client) and on **each machine you log in to** (the server).
The server end needs no root and no service: ssh starts it each time.

### 1. Build it (Linux)

```sh
# Debian or Ubuntu, with root: what the core needs to build
sudo apt-get install -y git build-essential autoconf automake pkg-config \
    protobuf-compiler libprotobuf-dev libncurses-dev libssl-dev zlib1g-dev
#   (no root? tools/deps.sh unpacks the same packages under .deps/ instead)

git clone --recurse-submodules https://github.com/wmacevoy/gosh
cd gosh
tools/build.sh            # builds into .build/bin: gosh-client, gosh-server, gosh, gocp
```

`tools/check.sh` runs every test, if you want to see them pass on your machine first (a few
minutes; it starts and stops only its own processes).

### 2. Put the programs where ssh finds them

```sh
mkdir -p ~/.local/bin
cp .build/bin/gosh-client .build/bin/gosh-server ~/.local/bin/
ln -sf gosh-client ~/.local/bin/gosh
ln -sf gosh-client ~/.local/bin/gocp
```

Do this on both machines. `gosh` looks in `~/.local/bin` on the server itself, so nothing more
is needed. (A command that ssh runs does not usually see `~/.local/bin`: Debian's and Ubuntu's
shell files add it only for a login. That matters only to other programs, such as a stock
`mosh`, asking for `gosh-server` or `mosh-server` there.)

### 3. Let ssh log in without asking

The relay and the meetup's ticket each open an ssh connection while the terminal is already the
session's, so ssh must not ask for anything (`REQ1/PATH-1`): use a key, with `ssh-agent`.
`ssh -o BatchMode=yes HOST true` must succeed.

### 4. Use it

```sh
gosh user@host                              # a terminal, as mosh
gosh -L 8000:localhost:8000 user@host       # and the server's port 8000 on yours
gosh -N -L 8000:localhost:8000 user@host    # the forward alone, no terminal
gocp report.pdf user@host:papers/           # a copy; run it again if it is cut off
gosh --relay=always user@host               # over ssh only, from the start, nothing by UDP
gosh -6 user@host                           # IPv6 only
```

`gosh --help` lists everything. With nothing more said, a session tries UDP, falls back to ssh
after 3 seconds of silence, looks for a way straight through from there, and asks no server on
the Internet anything (`REQ1/PATH-7`).

**A meetup** is for the case where the server has no public address, a desktop at home, and no
way straight through can be found: datagrams then go through a server of yours that both ends can
reach. Name it with `--meetup=USER@HOST:PORT`; [`AWS.md`](AWS.md) sets one up.
