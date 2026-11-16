# gosh

"gosh, a better mosh!?" — [mosh](https://mosh.org)'s session, with port forwards, file copies, an ssh
fallback and NAT traversal beside it, as comprehensible, tested parts.

What builds today is upstream's mosh at a pinned commit, untouched, with gosh's parts beside it:
`gosh host` logs in and opens a session; it forwards ports (`-L`, `-R`, `-LU`, `-RU`, `-N`);
`gocp` copies files and resumes where it was cut off; when UDP cannot get through, the session
goes over ssh and comes back; from there it finds a way straight through NATs, or through a
meetup on your own server; and all of it works by IPv6 as by IPv4. Either end can still be a
stock mosh, for the terminal (`tools/check.sh` tests all of this, on one machine).

- **[`docs/USING.md`](docs/USING.md)**: what you get, compared with mosh and with shpool, and how
  to build and set it up.
- **[`docs/AWS.md`](docs/AWS.md)**: your own AWS server, IPv6 only, to work on and as your meetup.

Not yet: a one-line install (you build from source), gosh's names in every message, and tests
between real machines behind real NATs. What gosh must do, and what it will not, is
`docs/REQUIREMENTS_v16.md`; what is being worked is `docs/QUEUE.md`.

gosh is not mosh and is not the mosh project's work. It carries mosh's code under the GNU General
Public License, version 3 (`COPYING`), and keeps its authors' notice (`LICENSE-MOSH`; gosh's own is `LICENSE-GOSH`). **It has not been
independently reviewed**; a review by a third party is required before a class uses it.

The editor extension is `../remote-gosh`. Its requirements and queue are here.
