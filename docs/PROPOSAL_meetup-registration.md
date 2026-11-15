# Proposal: gosh-meetup, a meetup that knows who it is passing for

**A proposal, not the design in force. Revision 5, 2026-11-15.** Nothing here is built yet,
but for its groundwork (TweetNaCl carried in, and gosh's wrapper of it: section 8).

The owner's decisions it is written to, in his words:

- "pf7 was a step to here. let it go. go for a tailscale. (or talescale-like if that breaks
  some ownership)"
- "tweet it is --- it's rare."
- "it seems like oauth to me ... i use oauth on the meetup server and it generates a
  (user_id,user_pub/user_sec) and passes meetup_pub,meetup_sign(user_id,user_pub). the
  meetup_pub could roll on 90 days, and only the last 4 are valid (re-auth 1/year for a new
  key), and a blacklist for recent keys? ... it leaves very little state the meetup service has
  to remember (a few signing keys created every 90 days, and a db of blacklist ids, kept for 1
  year)."
- On the agent's suggested changes to that: the user's machine makes its own key pair ("agree
  (no reason to keep the private key, but no reason to make it there either)"); no signer apart
  from the meetup ("that's just more state"); the last four keys kept whole ("you still need to
  check the older keys, so no, you need the last 4"); no opaque user id ("the sent message ...
  is encrypted by the meetup(time) public key, so it's opaque already. no pki --- that's just
  another layer"); no handing out of the meetup's address ("the meetup address is the service,
  no hand-out, you already found it. gosh-meetup is a service").
- "the start is a local public/secret key where the public key url-encoded to the
  mosh-meetup."
- "default domains are apple,google,microsoft".
- No forward secrecy for registrations ("ok"); the meetup's log naming users deferred ("no -
  deferred").

Revisions 1 and 3 were each attacked on paper by a session set to break them
([SE1/W4](../vendor/se1/guide/08-work.md#w4)); both verdicts were "build, with these changes",
and the changes are in. Revision 5 changes where a pass comes from, which neither attack saw:
**it should be attacked once more before it is built.** Section 10 says how it got here.

## 1. What is wrong today

A meetup has one key, shared by everyone allowed to use it. To have a datagram passed on, an
end puts twelve bytes in front of it: four random bytes and a tag of those four bytes. They
travel in the clear, say nothing of who sent them, when, or for which pair, and the meetup
treats whoever last named a pair as one of its ends. So anyone who sees one datagram can use
the meetup for ever and can take an end's place (`src/meetup/README.md`; `passing-test.cc`,
`test_replay`, with the real cipher).

## 2. The shape

**gosh-meetup is one service at one address.** It does two things:

- **Enrolment, on the web.** A user signs in with an Apple, Google or Microsoft account, and
  the service signs the public key the user's machine made: that signature is the user's
  **pass**.
- **Passing datagrams, by UDP**, for ends that show a pass and a key their user vouched for.
  It is what `gosh-server meetup` is today, grown.

It remembers almost nothing: its **term keys** (a new one every 90 days; the last four are
good) and a **blacklist** kept for a year. It keeps no list of users and no database of
passes: a pass is checked by its signature alone.

From Tailscale's relays, as the agent remembers their published design (none of its code or
wire format is used): nothing reusable crosses the wire in the clear; an end proves it holds a
private key, afresh; a datagram is passed to a key, not to whoever spoke last. From WireGuard,
likewise from memory: a cheap check that a sender knows the relay's public key, before any
costly work.

**Code carried in:** TweetNaCl (public domain), in `vendor/tweetnacl/`, for the box, the
signature and the hash; used when an end enrols or registers, never on a relayed datagram.

## 3. The keys

| Key | Kind | Who holds the private half | Who has the public half | Made |
| --- | --- | --- | --- | --- |
| a **term key** of the service | a signing key and a box key, made together | the service: the last four terms' | anyone who asks the service; an end learns them when it enrols | every 90 days, by the service |
| a **user's key**, U | signature | the user, in a file only they can read; it never leaves their machine | the service, which signs it | `gosh meetup-login`, once, the first time |
| a **session end's key**, E | box | one end of one session, in memory, for that session | the other end (over ssh), and the service | each relay, when it starts |
| a registration's key, `Kr` | mosh's cipher | one end and the service | — | the service, at random, when an end registers |

**Terms.** A term is a number the service counts upward, one for each 90 days of its life.
Term `t`'s keys are made when it begins; its private halves are destroyed when term `t + 4`
begins, so they live 360 days. The service holds terms `t - 3` to `t`. Which term it is, is the
service's own business: no other clock is asked to agree with it.

**The pass**: the service's signature, with term `t`'s signing key, over `("gosh pass v1", t,
U, user_id)`, where `user_id` is the account it signed in with (for example
`google:1234567890`). A pass is good while term `t` is among the last four, and while neither
`U` nor `user_id` is on the blacklist: nine months to a year, by when it was signed in its term.

**A voucher**: the user's signature with `U`, over `("gosh end v1", t, E, P)`: "under the pass
of term `t`, end `E` may be passed to and from end `P`". The client end makes both of its
session's vouchers, its own and the server end's; the server end sends its `E` over ssh and
gets its voucher and the pass back. So the server end never holds the user's private key. **A
voucher is for one pair:** it is of use only while `P`'s private key exists, which is one
session. A host that keeps the vouchers of old sessions holds nothing it can use.

**What an end seals to.** Everything an end sends while registering is boxed to the **box key of
the term its pass was signed in**. So the pass, `U` and `user_id` cross the network sealed: an
onlooker sees none of them. This is why no further hiding of the user's identity is needed.

## 4. Enrolling (on the web)

```
gosh meetup-login meetup.example.org
```

1. The first time, it makes the user's key pair and writes the private half to a file only the
   user can read (created anew, never overwritten, refused if anyone else can read it).
2. It opens, in the user's browser, the service's address with the public half in it:
   `https://meetup.example.org/enrol?key=<U, base64url>&back=<a port on 127.0.0.1>`.
3. The user signs in (Apple, Google or Microsoft, by default; whoever runs the service may
   change the list).
4. The service signs the pass and sends the browser back to `http://127.0.0.1:<port>/`, with
   the pass and the four term keys' public halves in the address. `gosh meetup-login` is
   listening there, for this one request, and writes them to a file beside the user's key.
   (This is how programs without a web browser of their own sign in: the browser does the web,
   and the answer comes back on the machine's own loopback. gosh needs no HTTPS of its own:
   `REQ1/SHAPE-3`.)
5. Where there is no browser on the machine (a login over ssh), the service shows the pass as
   text instead, and `gosh meetup-login --paste` takes it.

Nothing proves to the service that whoever enrols holds `U`'s private half, and nothing needs
to: a pass for a key one does not hold cannot be used, since every voucher must be signed with
it.

**Renewing:** an end that sees its pass's term is the oldest of the four says so, and
`gosh meetup-login` again gets a new pass (the same `U`, or a new one: the user's choice).

## 5. Registering and passing (by UDP)

UDP to the service's one port. Each kind of datagram has a mark of its own (8 bytes), a
version, and one exact length (but the two that carry a datagram). **Lengths are checked before
anything is opened**, the cipher's included: it guards its buffer with an `assert` that some
builds leave out. `M` below is the box key of term `t`, the term of the end's pass.

### 5.1 Registering (two round trips, for an end the service does not know)

```
end -> meetup   HELLO       mark | t (4) | E (32) | nonce (24) | gate (16)
meetup -> end   COOKIE      mark | the HELLO's nonce (24) | cookie (24)
end -> meetup   REGISTER    mark | t (4) | E (32) | nonce (24) | cookie (24) | boxed E -> M:
                                { kind, version, the cookie again, P (32), U (32),
                                  user_id (1 + up to 64), the voucher (64), the pass (64) }
meetup -> end   REGISTERED  mark | nonce (24) | boxed M -> E:
                                { kind, version, the cookie again, id (8), Kr (16),
                                  where the end appears to be: family (1), address (16), port (2),
                                  the four terms' public keys if they have changed since t }
        or      REFUSED     mark | nonce (24) | boxed M -> E: { kind, version, the cookie again, why (1) }
```

Cheapest first, and where it stops:

1. **HELLO.** If `t` is not one of the last four terms: no answer. `gate` is the first 16 bytes
   of the hash of a label, `M`, `E` and the nonce: one hash. A sender who does not know `M`
   gets no answer and learns nothing. No state is kept.
2. **COOKIE**, in the clear: the service's own time since it started (never the time of day,
   never backward) and a tag, under a secret it makes afresh each start, over that time, the
   HELLO's source address and `E`. One hash. Good for ten seconds, at that address, for that
   `E`: it shows the end receives where it sends from, and makes the REGISTER fresh, with no
   clocks agreeing.
3. **REGISTER.** The cookie is outside the box and is checked first: one hash. Then a limit on
   how many REGISTERs one address may have opened per second. Only then the box (the first
   costly step), which shows the sender holds `E`'s private half; then the two signatures; then
   the blacklist.
4. It is taken if: `P` is not `E`; the pass is term `t`'s signature over `t`, `U` and
   `user_id`; neither `U` nor `user_id` is blacklisted; and the voucher is `U`'s signature over
   `t`, `E` and `P`. It makes a **registration**: `E` at that address, for `P`, of `U` and
   `user_id`, under term `t`, with a random `id` and a new `Kr`. An `E` registered already: an
   older cookie is ignored; the same cookie gets the same answer again.
5. Whatever fails after a good cookie is answered REFUSED, saying why (the pass's term has
   passed; the user is blacklisted; the service is full), since the asker has had an answer
   already and silence would leave nobody able to tell a blacklisted student from an outage.
   Whatever fails before gets no answer.

The end takes a COOKIE only if it carries the nonce of a HELLO it has outstanding, and a
REGISTERED or REFUSED only if it carries the cookie it sent. Each side checks the direction bit
of the nonce it receives. A box whose shared secret comes out all zero is refused.

### 5.2 Keeping a registration (one round trip, no public-key work)

```
end -> meetup   KEEP    mark | id (8) | counter (8) | sealed under Kr: { kind, version }
meetup -> end   KEPT    mark | counter (8)          | sealed under Kr: { kind, version, where the end appears to be }
```

- The service looks at the blacklist and at the term again; if the user is on the one, or the
  term has passed, it drops the registration and says nothing. Otherwise it is good for two
  more minutes.
- An end holds a registration for as long as path-finding is running, direct path or no, and
  sends KEEP every 25 seconds, give or take a few.
- **It starts again from HELLO** when a KEEP or a PASS has had nothing back for 3.5 seconds,
  when KEPT shows its address has changed, or when REGISTERED never came: after 3.5 s, 7 s,
  14 s, ... up to a minute, each varied a little, so that the ends of a service that restarted
  do not all arrive at once.

### 5.3 Passing a datagram on

```
end -> meetup   PASS     mark | id (8) | counter (8) | the datagram, sealed under Kr with that counter
meetup -> end   PASSED   mark | the datagram
```

- In this order: find `id`; check the source address is that registration's; check the
  length; open the seal; then mark `counter` used (a window of 1,024). Send PASSED to the
  registration of the `P` this one is for, **if that one is for this one's `E`**, at its
  address, if heard within two minutes.
- Counters under one `Kr` only go up, the end's and the service's told apart by one bit; a
  replaced registration gets a new `Kr`.
- An end takes a PASSED only from the service it is registered with.
- At most 2,032 bytes of datagram; a session's are about 1,300.

### 5.4 How many, and who is put out

At most 2,048 registrations (about 10 MB), 16 for one user (eight sessions). A user's
seventeenth puts out **that user's registration that has gone longest without passing a
datagram to an end that is for it**: what a user is using stays, and what somebody holds in
their name goes first. When the service is full, a new user's end is refused, and told so.

## 6. What an attacker can do

| Attacker | Today | Proposed |
| --- | --- | --- |
| Knows nothing of the service | Gets no answer | Gets no answer by UDP. (The web side is a web page.) |
| Knows a term's public key; holds no pass | Uses the meetup for ever with one copied datagram; takes an end's place | Gets cookies; can make the service do one hash per datagram, and from an address it really holds, a limited number of box openings a second; can see that some key `E` registers every 25 s, from where (not whose). Cannot register, pass, or take a place |
| Sees traffic and can forge addresses; no pass | As above | Can replay to the service only what it did not receive (delivered late). Can answer an end first with a false COOKIE each time and so keep it from registering: a denial by someone on the path |
| Anyone with an Apple, Google or Microsoft account | — | Can enrol, and use the service, as themselves. **That is most people.** The blacklist is what keeps someone out; a service for only its own people narrows the list of providers |
| Another user | Takes an end's place, if they see the pair's name | Uses the service. Cannot open another's messages, register as another's end, or be passed another pair's datagrams. Spends only their own 16 |
| A server host the user logged in to | Holds the shared key: a user of the meetup for ever | For that session, is that session's server end, as it must be. Afterwards holds a voucher that names an end nobody is: it can keep that registration alive until it is put out first (5.4). Cannot relay for itself |
| Someone who steals a user's private key | — | Is that user, until `U` or the account is blacklisted, or the pass's term passes. Cannot read or join a running session |
| Someone who takes over a user's account at the sign-in provider | — | Can enrol a key of their own as that user, until the account is blacklisted |
| Someone who breaks into the service | Sees which addresses talk and how much | Can sign passes with the current term's key and read registrations sealed to the four terms' keys they find (which user, at which address, for which peer, each `Kr`), until those keys are replaced: then everyone signs in again. Not the content of any session |
| Anyone, with huge or malformed datagrams | (the fork never opened what it was sent) | Refused by length before anything is opened; what fails to open is caught |

In every row, nobody reads or changes what the two ends say: that is sealed by the session.

## 7. Each input: absent, late, wrong, lying

- **Absent.** The service down: no COOKIE, and the end's log says "no answer to hello"; the end
  goes on with its other paths and asks again (5.2). No pass on the machine: the end does not
  try the service, and `gosh` says to run `gosh meetup-login`. The other end not registered, or
  not for this one: datagrams are dropped. The sign-in provider down: enrolment waits; passes
  already given go on working.
- **Late.** A cookie older than ten seconds is refused; the end asks for another. An old PASS is
  refused once 1,024 behind. A pass whose term has passed is refused, saying so, and the end
  says to sign in again.
- **Wrong.** Before a good cookie, anything wrong gets no answer. After one: REFUSED with why, or
  (a box that does not open, a voucher that is not that user's for this term and pair) dropped.
- **Lying.** An end for a `P` that is not for it is passed nothing. A user can vouch for any
  pair of keys: that is what a user is allowed to do, and it spends their own 16.
- **The service's own state** (the four terms' keys, the blacklist) is files, read at start and
  again on a signal. Missing or unreadable at start: it does not start. Unreadable later: the
  old state stays, and it says so. Whoever edits the blacklist writes a new file and renames it.

## 8. What this does not close

- **Whoever breaks into the service** can sign passes and read the registrations sealed to the
  keys they find (6). There is no forward secrecy for that (the owner: "ok"). The remedy is new
  term keys, after which everyone signs in again. *The agent's recommendation, not decided:*
  run the web side and the UDP side as two processes of the one service, sharing the key files,
  so that a fault in the side that faces every datagram on the Internet does not hand over the
  signing key. It adds no state and nothing a user sees.
- **The service is trusted** not to drop, and sees who talks to whom, how much, and as which
  user. It writes none of that to its log (deferred by the owner), so whoever runs it cannot
  tell from it whom to blacklist; the sign-in pages know who enrolled.
- **The default sign-in providers admit almost anyone** (6).
- **A PASSED proves nothing to the end that gets it.** Anyone forging the service's address can
  send one; the session refuses what is in it.
- **An end's key `E` is in the clear** when it registers, so one session can be followed across
  addresses by it. mosh's own datagrams already carry a counter in the clear.
- **Anyone who knows a term's public key can spend the service's time:** a hash per datagram,
  and a capped number of box openings from each real address.
- **A NAT that changes an end's port** costs 3.5 seconds and two round trips to mend, where a
  session of mosh's own follows a client at once.
- **A user's private key is a file** with no passphrase: any program running as that user can
  vouch.
- **TweetNaCl** is small and slow; its signature check accepts some signatures in more than one
  form, and it does not refuse a few degenerate keys. Neither matters here as long as nothing
  is remembered by a signature's bytes; gosh's wrapper refuses degenerate box keys.
- **IPv4.** The UDP side listens on IPv4, as today; the answer's address field fits IPv6.
- **Cost.** Each relayed datagram is sealed once more by the sender and opened once by the
  service, with mosh's cipher. This path is the last resort before ssh.

## 9. What changes in gosh, and what is new

**Done already** (groundwork): `vendor/tweetnacl/` (TweetNaCl as published, checked against
published answers in the check); `src/meetup/nacl.*` (the one wrapper that calls it: every
size checked, degenerate keys refused, the program stopped if there are no random bytes).

**In gosh** (C++, this repository):

- `src/meetup/`: `proof.*` goes. New logic, each with a test of bytes and numbers and no
  socket: `keys.h` (passes and vouchers made and checked; terms; the blacklist), `cookie.h`,
  `messages.h` (every kind made and opened; exact lengths), `registrations.h` (the table:
  5.1, 5.2, 5.4), `passing.h` (5.3). `serve.cc` stays the edge: the socket, the service's own
  time, the state files.
- `gosh meetup-login HOST` (and `--paste`): makes the user's key the first time, opens the
  browser, listens once on loopback for the pass, writes it. A small reader of one HTTP request
  on loopback; no TLS.
- Between the two relays over ssh: after its greeting the server end sends its `E`; the client
  end answers with the service's address, the term, the voucher and the pass. Old relays
  ignore words they do not know.
- `src/pathfinding/`: the end's half, as logic, when that part is reshaped (the rest of item 8).
  **In the fork today, found by the first attack:** `punch.cc` hands any probe-marked datagram
  of up to 65,528 bytes to the cipher's `decrypt`, whose 2,048-byte buffer is guarded only by
  an `assert`. Path-finding is off in gosh; this is fixed before it is switched on.
- Tests: the attacks of 2026-11-15 turned round (each thing the onlooker could do becomes a
  check that they cannot), and one for each row of section 6 that a test can reach.
- No private key, `Kr`, pass, voucher or the bytes of a registration is written to a log.

**New, outside this repository:** the **web side** of gosh-meetup, which signs users in and
signs passes. It shares no code with gosh and needs a web server and the sign-in providers'
libraries, so *the agent's recommendation, not decided* is a repository of its own, as the
editor extension is. It reads and writes the same term-key files as the UDP side, and the
blacklist. It holds no list of users.

A gosh meetup and the fork's relays ignore each other; the requirements already say gosh does
not work with the fork's releases at the other end.

## 10. How this proposal got here

- **Revision 1**: one shared key and the clock. Attacked: twelve findings (among them that
  someone with the shared key who saw one registration kept that pair from anywhere; an
  oversized datagram would abort the meetup inside the cipher; a tag over a counter alone let a
  payload be swapped; the clock rule failed silently and reopened replays on restart).
- **Revision 2** took them, with a key per user derived from a master; it kept to a shared-key
  cipher because the agent read `REQ1/SHAPE-3` as ruling out public keys. **That was a
  mistake:** it rules out a library a machine may lack, not source carried here.
- **Revision 3**: key pairs, authorities that sign users' keys by hand, a blacklist. Attacked:
  eleven findings (a voucher bound to nothing let a server host lock a user out for good; every
  stranger's datagram cost a costly step; answers were not tied to their requests; renewing
  with signatures would not fit; silence for every failure).
- **Revision 4** took those: vouchers that name the pair; a gate and a cookie before any costly
  step; answers that echo their requests; keeping without public-key work; refusals that say why.
- **Revision 5** (this): where a pass comes from, at the owner's direction. No authorities and
  no list of them: the service is its own signer, with term keys that roll every 90 days, the
  last four good, so that passes expire and the blacklist can be forgotten after a year; users
  enrol with an account they have, from a key their machine made. Everything revision 4 took
  from the attacks is kept.

## 11. Questions for the owner

1. **Two processes**, a web side and a UDP side, sharing the key files (section 8, first item):
   agreed?
2. **The web side in a repository of its own** (section 9): agreed?
3. **Attack revision 5 once more** before building, as revisions 1 and 3 were: agreed? (It
   changes the keys, which is where the last two attacks found the most.)
