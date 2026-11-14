# gosh

"gosh, a better mosh!?" — [mosh](https://mosh.org)'s session, with port forwards, file copies, an ssh
fallback and NAT traversal beside it, as comprehensible, tested parts.

**Only the first slice is here.** What builds today is upstream's mosh at a pinned commit, with
gosh's seam applied and the fork's port forwarding carried over: `gosh-client` and `gosh-server`
forward ports and still hold a session with a stock mosh, `gosh host` logs in and opens a session,
and `gocp` copies files (`tools/check.sh`). The ssh fallback and NAT traversal are not here yet. gosh is being migrated, part by part, from `wmacevoy/mosh` at
`mosh-1.4.0-pf.7`, which works today and is what gosh is measured against. What gosh must do, and
what it will not, is `docs/REQUIREMENTS_v12.md`; what is being worked is `docs/QUEUE.md`.

gosh is not mosh and is not the mosh project's work. It carries mosh's code under the GNU General
Public License, version 3 (`COPYING`), and keeps its authors' notices. **It has not been
independently reviewed**; a review by a third party is required before a class uses it.

The editor extension is `../remote-gosh`. Its requirements and queue are here.
