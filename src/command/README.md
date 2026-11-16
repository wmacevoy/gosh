# the command

`gosh` and `gocp`: log in by ssh, start the server, and return the client's arguments, so that the
same process carries on as the client (`REQ1/CMD-1`, `REQ6/NAME-1`; design v2, 2.2). gosh-client
is the command when it is run under either name, or with either as its first word.

**Divided** (2026-11-16, queue item 10). The fork's `moshwrapper.cc` (`mosh-1.4.0-pf.7`,
`src/frontend/`; 1,350 lines) is now the pieces design v2 names. Every word, message and behaviour
is the fork's, with gosh's changes for `REQ8/CMD-3` and the relay's server kept:

| File | What it holds | Kind | Test |
| --- | --- | --- | --- |
| `options.*` | the option table; the words read into settings (a name cut short, `no-`, a value after `=` or next); what is refused, in mosh.pl's order; `--meetup` split for the relays and for ssh (HOST a name, an IPv4 address, or an IPv6 one in brackets; PORT 1 to 65535: anything else refused); the server's port or range; a few text helpers | logic | `options-test.cc` |
| `words.*` | what was asked: which end of a copy is which and what each is told; `--stdio`'s command; the server's words, and a stock server's; ssh's; the relay's and the meetup ticket's commands; the client's words and the variables it and its relay read | logic | `words-test.cc` |
| `startup.*` | the startup message, a line at a time: `MOSH IP`, `MOSH SSH_CONNECTION`, `MOSH CONNECT`, which server answered; what is refused; the address and key at the end | logic | `startup-test.cc` |
| `stock.*` | the line that picks the host's server; what is said when it is a stock mosh's; gosh-server's line for the relay and the ticket (`helper_command`) | logic | `stock-test.cc` |
| `login.*` | runs ssh, or a local shell, and reads what the host says; asks the terminal its colours | edge | the outside tests |
| `proxy.*` | ssh's ProxyCommand: this program's path, the host's addresses by `--family`, and the bytes carried | edge | the outside tests |
| `moshwrapper.*` | `run`: the pieces in order, the usage texts, and the two places the command touches the world itself: the environment it reads (`MOSH_PREDICTION_DISPLAY`, the locales) and the variables it sets for the client | — | the outside tests |

The logic takes no environment: what it needs of it is passed in (the prediction mode, the
locales), and the variables for the client come back as a list that `run` sets. They are still
set as variables, not handed over as a value, because the client and its relay are the core's
`mosh-client` main and the relay's processes, which read them so; and a stock `mosh` script, or
the extension, starts `gosh-client` the same way.

Tested from outside, in `tools/check.sh`: `tests/gosh-command.py`, `tests/gosh-stock.py`,
`tests/mocp.test`, `copy-cut.py` with `gocp`, and `tests/wrapper.test`, run against pf.7's command
(as `mosh` and `mocp`) and against gosh's (as `gosh` and `gocp`), with checks of gosh's alone:
a host or a copy's remote end beginning with `-` is refused and ssh never run with it, and the
old words `stun` and `stun-key` refused and their new ones named.

What it refuses to give ssh (the review of item 10, 2026-11-16): a host, a `-l` user, a copy's
remote end or the meetup's login that begins with `-`, which ssh would take for an option (as
OpenSSH 9.6 and later refuse too); a `%` in this program's own path is written `%%` in the
ProxyCommand. `--stun=none` keeps the session off the meetup. A one-letter host is a host
(`x:path`), as scp has it; `user@[2001:db8::1]:path` names an IPv6 one.

The remote relay and the meetup's ticket are `gosh-server`'s, from `~/.local/bin` first
(`stock.h`'s `helper_command`), unless the user names the server. Until 2026-11-16 they ran
`mosh-server` by name, which on a host with a stock mosh as well is the stock one; the fallback
then never came up (`tests/gosh-stock.py`, "a host with both, over ssh").

Still to do in queue item 10: its usage text and messages still say `mosh` and `mocp`, and its
help has leftover Perl in it (`docs/FINDINGS.md`, 2026-11-13); the names change in one go with
the wire's last change.
