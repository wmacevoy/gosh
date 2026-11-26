# stun

STUN: the question "what address do I appear to have from outside?", and its answer, as bytes
that anyone may send (RFC 5389's Binding; `REQ15/PATH-4`). A part of its own since 2026-11-26
(the owner: "STUN is a domain"; `../../docs/PROPOSAL_domain-map.md`): path-finding asks it, and
the meetup answers it, and neither reaches into the other for it.

**Its ABI** (`../DOMAINS`):

- `stun.h`: a request made; an answer read (`stun_parse`, the edge that makes what
  `Strong::Stun::StunAnswer` kept a socket address); and an address as text, `a.b.c.d:p` or
  `[v6]:p`, read and written (`parse_ip_port`, `format_ip_port`), which the relay uses too.
- `../strong/stun/stunrequest.h`: `Strong::Stun::is_stun_request_checked`, whether bytes are a
  Binding Request, proved; the meetup asks it of every datagram.

**Its inside:** `../strong/stun/stunanswer.h`, `Strong::Stun::StunAnswer`, the answer read from
bytes, proved (`../strong/README.md`).

| File | What it holds | Test |
| --- | --- | --- |
| `stun.h` | STUN, asking: the request; `stun_parse`, the edge that makes what `Strong::Stun::StunAnswer` kept a socket address; `a.b.c.d:p` as text. Logic | `stun-test.cc`, reading an answer the meetup's `answer.h` wrote |

**Fuzzed** (queue item 21):

- `stun-fuzzer.cc`, the fork's: STUN answers from anyone, and addresses as text, written back the
  same. Seen red under AddressSanitizer with an answer longer than it says allowed (`stun.h`);
  breaking the check of each attribute's length alone shows nothing (the whole answer's length is
  checked first).

The check also replays it under AddressSanitizer (`tools/asan-replay.sh`). Seen: with `stun.h`
letting an attribute run 8 bytes past the body, the plain replay passes and that one stops on a
heap-buffer-overflow (the reviewer's input, `attr-past-body`).

`stun-test.cc` reads an answer that the meetup's `answer.h` wrote: a test of both, the one
crossing into another part's inside left here (`../DOMAINS`, its `allow` line).
