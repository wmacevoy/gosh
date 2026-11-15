# Proposal: gosh-meetup, a meetup that knows who it is passing for

**A proposal, not the design in force. Revision 6, 2026-11-15.** Nothing here is built yet,
but for its groundwork (TweetNaCl carried in, and gosh's wrapper of it: section 10).

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
  from the meetup ("that's just more state"); the last four keys kept ("you still need to check
  the older keys, so no, you need the last 4"); no opaque user id ("the sent message ... is
  encrypted by the meetup(time) public key, so it's opaque already. no pki --- that's just
  another layer"); no handing out of the meetup's address ("the meetup address is the service,
  no hand-out, you already found it. gosh-meetup is a service").
- "the start is a local public/secret key where the public key url-encoded to the
  mosh-meetup."
- "default domains are apple,google,microsoft".
- Two processes, "yes"; the web side in a repository of its own, "yes"; attack it again before
  building, "yes". "whitelist - blacklist, both a set of regular expressions." "use sqlite
  regex, it's bounded and a nice place to keep the config." And then, of SQLite in gosh: "not
  needed here, that's requirements for the web service. here the config can be ~/.config/gosh or
  whatever windows likes. plain text here like all the other keys."
- No forward secrecy for registrations ("ok"); the meetup's log naming users deferred ("no -
  deferred").

It has been attacked on paper three times by sessions set to break it
([SE1/W4](../vendor/se1/guide/08-work.md#w4)): revisions 1, 3 and 5. Each verdict was "build,
with these changes", and the changes are in. Section 12 says what each round found. **Four
questions for the owner are in section 13**; two of them touch earlier decisions.

## 1. What is wrong today

A meetup has one key, shared by everyone allowed to use it. To have a datagram passed on, an
end puts twelve bytes in front of it: four random bytes and a tag of those four bytes. They
travel in the clear, say nothing of who sent them, when, or for which pair, and the meetup
treats whoever last named a pair as one of its ends. So anyone who sees one datagram can use
the meetup for ever and can take an end's place (`src/meetup/README.md`; `passing-test.cc`,
`test_replay`, with the real cipher).

## 2. The shape

**gosh-meetup is one service at one address**, run as two processes under two Unix users:

- **the web side** enrols users: they sign in with an Apple, Google or Microsoft account, and
  it signs the public key their machine made. That signature is a **pass**. It lives in a
  repository of its own (section 10).
- **the UDP side** passes datagrams for ends that show a pass and a key their user vouched for.
  It is what `gosh-server meetup` is today, grown.

What it keeps: its **term keys** (section 3); on the web side, **one SQLite database** with the
web side's settings, its whitelist and its blacklist; on the UDP side, **plain text files**, as
gosh keeps all its keys and settings (section 4). It keeps no list of users and no database of
passes: a pass is checked by its signature alone.

**Where gosh keeps things.** Everything gosh itself keeps, on a user's machine or on the machine
that runs the UDP side, is plain text in gosh's own directory: `~/.config/gosh/` (or
`$XDG_CONFIG_HOME/gosh/`) on Linux and BSD, `~/Library/Application Support/gosh/` on macOS,
`%APPDATA%\gosh\` on Windows. Keys are files only their owner can read.

From Tailscale's relays, as the agent remembers their published design (none of its code or
wire format is used): nothing reusable crosses the wire in the clear; an end proves it holds a
private key, afresh; a datagram is passed to a key, not to whoever spoke last. From WireGuard,
likewise from memory: a cheap check that a sender knows the relay's public key before any
costly work.

**Carried in:** TweetNaCl (public domain), for the box, the signature and the hash, used when
an end enrols or registers, never on a relayed datagram. SQLite is the web side's, not gosh's.

## 3. Keys, and terms

| Key | Kind | Private half held by | Lives | Public half known to |
| --- | --- | --- | --- | --- |
| a term's **signing key** | signature | the web side alone | destroyed when the term ends: 90 days | the UDP side, and every end that enrols |
| a term's **box key** | box | the UDP side alone | destroyed four terms later: 360 days | every end that enrols |
| a **user's key**, U | signature | the user, in a file only they can read; never leaves their machine | as long as the user likes | the service, which signs it |
| a **session end's key**, E | box | one end, in memory, for one session | one session | the other end (over ssh), and the UDP side |
| a registration's key, `Kr` | mosh's cipher | one end and the UDP side | one registration | — |

Only the current term's signing key is ever used, so only it is kept; a break-in can mint passes
only under it. Box keys are kept for the life of the passes that seal to them.

**Terms.** A term has an **id**: the first eight bytes of the hash of its two public keys. Ends
and both sides know terms by their ids, never by a count, so a term cannot be mistaken for
another even if the service loses everything and starts again. Terms are also ordered by the
date they began, which only the service's tool reads.

**Rolling** is done by one tool, `gosh-meetup term`, run by whoever runs the service (by hand,
or every day from cron; it does nothing on most days):

1. A term's keys are made seven days before it is due, and written as one file, then renamed.
2. The UDP side looks at the key directory every few seconds (it reads the directory's change
   time; nothing more), loads a new term, and writes a mark that it has.
3. The web side signs with a new term only once that mark is there. So no pass is ever signed
   that the UDP side cannot check.
4. It never rolls more than one term in a run, and refuses to roll if the clock reads earlier
   than the newest term began (a machine that has lost its clock does not destroy four terms of
   keys at once).
5. It exits non-zero, saying what, on any step it could not do.

`gosh-meetup term --now` is the **emergency**: a new term at once, and the old ones dropped.
Everyone signs in again. It has a test.

**Backups** hold public halves and the database, never a private key. A lost key directory is an
emergency roll.

**The pass**: the current term's signature over, in this order and these sizes:

```
"gosh pass v1"         12 bytes
service name           1 byte of length, then up to 255 bytes (its address, as users type it)
term id                8 bytes
U                      32 bytes
account                1 byte of length, then up to 255 bytes: provider ":" address
stable id              1 byte of length, then up to 255 bytes: the provider's issuer and subject
```

An account name or stable id longer than 255 bytes is refused, never cut short. Test vectors for
this exact encoding are shared by both repositories ([SE1/T11](../vendor/se1/guide/06-testing.md#t11)).
*The stable id is the agent's recommendation and the owner's to decide (section 13, question 1).*

**A voucher**: the user's signature with `U` over `"gosh end v1"`, the service name, the term id,
`E` and `P`: "at this service, under that pass, end `E` may be passed to and from end `P`". The
client end makes both its session's vouchers, its own and the server end's, **once each per
session**; the server end sends its `E` over ssh and gets its voucher back, and never holds the
user's private key. A voucher is for one pair at one service: of use only while `P`'s private
key exists, which is one session.

**What an end seals to.** Everything an end sends while registering is boxed to its pass's
term's box key, so the pass, `U`, the account and the stable id cross the network sealed.

## 4. Who may enrol: the whitelist and the blacklist

**On the web side**, which is its own repository with its own requirements: both lists are sets
of regular expressions in its SQLite database, matched with SQLite's `REGEXP` (its extension
`ext/misc/regexp.c`: *from the agent's memory, to be checked against its source, an automaton
whose time is bounded by the length of the text times the length of the expression*). What gosh
asks of that side, because both sides rely on it:

- an expression is matched against the whole text (`school\.edu` does not match
  `myschool.edu.example`);
- the account name is lower-cased, and anything outside printable ASCII, a space or a control
  character is refused, before anything is signed or matched;
- the whitelist decides who may enrol; out of the box, `(apple|google|microsoft):.*`;
- a whitelist of a school's domain is checked against what the provider says the account's
  organisation is (section 5, step 4), not only against the address's suffix.

**On the UDP side**, in gosh: the **blacklist** it must check at every registration and keep is a
plain text file in gosh's directory, one entry a line, **exact text, not expressions**: an account
name (`id:google:alice@school.edu`), or one machine's key (`key:<U, base64url>`), each with the id
of the term it was added in. The web side writes it when its own blacklist matches someone who has
enrolled; whoever runs the service may write it by hand. The UDP side looks at the file's change
time every few seconds and reads it again when it changes, keeps for each registration whether
the list named it, and drops an entry four terms after it was added, when every pass it could
name has expired. So gosh carries no regular-expression engine and no database, and nothing on a
datagram's path matches anything.

Under the default whitelist anyone with an account at the three providers can enrol, and a
blacklisted person can make a new account in a minute. **The blacklist means something only
behind a narrow whitelist.** Section 9 says so.

## 5. Enrolling (on the web)

```
gosh meetup-login meetup.example.org
```

1. The first time, it makes the user's key pair and writes the private half to a file only the
   user can read (created anew, never overwritten, refused if anyone else can read it). It
   prints the key's fingerprint as **four words**.
2. It makes a random 128-bit `state`, listens on a fresh port of 127.0.0.1, and opens in the
   user's browser `https://meetup.example.org/enrol?key=<U>&state=<state>&port=<port>`.
3. The web side checks `port` is a number from 1 to 65535, keeps `U`, `state` and `port` on its
   own side against a session of its own (never read back from what the browser sends later),
   and sends the user to sign in, **always asking them to choose the account** (`prompt=
   select_account`), with its own `state`, an OpenID `nonce`, and PKCE.
4. When the provider sends the user back, the web side checks, refusing on any failure:
   - the ID token's signature, against the provider's published keys;
   - `iss` is exactly the provider's; `aud` is the service's own client id; `exp` and `iat` are
     in range; `nonce` is the one it sent; its own `state` matches;
   - `email_verified` is true;
   - **Google:** a whitelist of a domain is checked against the `hd` claim, not the address;
   - **Microsoft:** the address is taken only if the optional claim `xms_edov` is true (the
     address's domain is verified as the tenant's own), or if the tenant `tid` is on a list the
     service keeps; personal accounts carry the one fixed consumer `tid`;
   - **Apple:** the reply comes as a form posted across sites, so the web side's session is held
     on its own side and not in a cookie that such a post would not carry;
   - the account matches the whitelist and not the blacklist.
5. **A confirmation page**: "You are enrolling a machine for `google:alice@school.edu`. Only
   continue if you have just run `gosh meetup-login`, and it shows these words:" and the four
   words of `U`. One button.
6. The web side signs the pass and sends the browser a page that posts it, with every term's
   public keys and the `state`, to `http://127.0.0.1:<port>/`: the web side builds that address
   itself and never sends a browser anywhere else. A posted form keeps the pass out of the
   browser's history and the server's logs.
7. `gosh meetup-login` takes the first request **that carries its `state`** and ignores every
   other, checks the Host header is 127.0.0.1, and checks the pass: that it signs this service,
   **its own `U`**, under a term whose signing key came with it, and that any term it already
   knew has the same keys. Then it writes the pass and the keys beside the user's key, and stops
   listening.
8. **Without a shared loopback** (a login over ssh, WSL, a container, an editor running remotely),
   the page also shows the pass as text beside the button, in large type: "Nobody legitimate will
   ever ask you for this. Paste it only into the `gosh meetup-login --paste` you just started." The
   pasted text is checked exactly as in step 7.

**Renewing.** REGISTERED and KEPT carry the current term's id. An end whose pass is in the
oldest term says, on every start, "your gosh pass expires soon: run gosh meetup-login".

The web side turns off its server's request log for `/enrol` and its callbacks (their addresses
carry `U`), and sends `Referrer-Policy: no-referrer`.

## 6. Registering and passing (by UDP)

UDP to the service's one port. Each kind of datagram has a mark of its own (8 bytes), a version,
and one exact length but the two that carry a datagram. **Lengths are checked before anything is
opened**, the cipher's included: it guards its buffer with an `assert` that some builds leave
out. `M` below is the box key of the end's pass's term.

### 6.1 Registering (two round trips, for an end the service does not know)

```
end -> meetup   HELLO       mark | term id (8) | E (32) | nonce (24) | gate (16)
meetup -> end   COOKIE      mark | the HELLO's nonce (24) | cookie (24)
             or EXPIRED     mark | the HELLO's nonce (24) | cookie (24)
end -> meetup   REGISTER    mark | term id (8) | E (32) | nonce (24) | cookie (24) | boxed E -> M:
                                { kind, version, the cookie again, P (32), the pass and the
                                  fields it signs (section 3), the voucher (64) }
meetup -> end   REGISTERED  mark | nonce (24) | boxed M -> E:
                                { kind, version, the cookie again, id (8), Kr (16),
                                  where the end appears to be: family (1), address (16), port (2),
                                  the current term's id (8), and all four terms' public keys }
        or      REFUSED     mark | nonce (24) | boxed M -> E: { kind, version, the cookie again, why (1) }
```

Cheapest first, and where it stops:

1. **HELLO.** `gate` is the first 16 bytes of the hash of a label, the term's box public key, `E`
   and the nonce. The UDP side keeps the box public keys of the last **eight** terms. If the
   term is one of the last four and the gate checks, it goes on. If the term is one of the four
   before those (expired) and the gate checks, it answers **EXPIRED**, unsealed: the one refusal
   it can still send for a term whose private key is gone. Anything else gets no answer.
2. **COOKIE**, in the clear: the UDP side's own time since it started (never the time of day,
   never backward) and a tag, under a secret it makes afresh each start, over that time, the
   HELLO's source address and `E`. Good for ten seconds, at that address, for that `E`.
3. **REGISTER.** The cookie is checked first (one hash). Then a limit on costly opens per
   address, and **a limit on costly opens for the whole service** (a token bucket; passing
   datagrams is served first). Only then the box, with the key agreement done once for this `E`
   and kept for the answer; then the two signatures; then the blacklist, if the account and key
   have not been matched since the list last changed.
4. Taken if: `P` is not `E`; the pass is the term's signature over this service, the term, `U`,
   the account and the stable id; the blacklist matches neither the account nor `U`; and the
   voucher is `U`'s signature over this service, the term, `E` and `P`. It makes a
   **registration**: `E` at that address, for `P`, of that account, under that term, with a
   random `id` and a new `Kr`. An `E` registered already: an older cookie is ignored; the same
   cookie gets the same answer again.
5. After a good cookie, whatever fails is answered REFUSED with why (the user is blacklisted; the
   service is full; the user's registrations are over their limit). Before one, nothing.

The end takes a COOKIE or EXPIRED only if it carries the nonce of a HELLO it has outstanding,
and a REGISTERED or REFUSED only if it carries the cookie it sent. EXPIRED is not sealed, so
anyone on the path could send one; it only makes the end tell the user to sign in again, which
an attacker on the path could prevent anyway by dropping. Each side checks the direction bit of
the nonce it receives. A box whose shared secret is all zero is refused. On EXPIRED the end
prints, every time, "your gosh pass has expired: run gosh meetup-login", and stops trying.

### 6.2 Keeping a registration (one round trip, no public-key work)

```
end -> meetup   KEEP    mark | id (8) | counter (8) | sealed under Kr: { kind, version }
meetup -> end   KEPT    mark | counter (8)          | sealed under Kr: { kind, version,
                                                         where the end appears to be, the current term's id }
```

- If the term has expired, or the blacklist now matches, the registration is dropped and the
  next HELLO says why (EXPIRED, or REFUSED after a cookie).
- An end holds a registration for as long as path-finding is running and sends KEEP every 25
  seconds, give or take a few.
- **It starts again from HELLO** when a KEEP or a PASS has had nothing back for 3.5 seconds, when
  KEPT shows its address has changed, or when REGISTERED never came: after 3.5 s, 7 s, 14 s, ...
  up to a minute, each varied a little.

### 6.3 Passing a datagram on

```
end -> meetup   PASS     mark | id (8) | counter (8) | the datagram, sealed under Kr with that counter
meetup -> end   PASSED   mark | the datagram
```

- In this order: find `id`; check the source address; check the length; check the
  registration's and its user's rate (below); open the seal; then mark `counter` used (a window
  of 1,024). Send PASSED to the registration of the `P` this one is for, **if that one is for
  this one's `E`**, at its address, if heard within two minutes.
- Counters under one `Kr` only go up, the end's and the service's told apart by one bit; a
  replaced registration gets a new `Kr`.
- An end takes a PASSED only from the service it is registered with.
- At most 2,032 bytes of datagram; a session's are about 1,300.
- **Rate.** Each registration, and each account, may pass so many datagrams and bytes a second.
  The figures are set from what a mosh session actually needs, measured, and are the service's
  settings in its database.

### 6.4 How many, and who is put out

At most 2,048 registrations (about 10 MB), 16 for one account, and a limit for one sign-in
domain, all in the service's settings.

- A registration whose `P` has not registered within 60 seconds is dropped: it has nobody to pass
  to.
- An account's seventeenth puts out **that account's** registration that has gone longest
  without passing a datagram to an end that is for it.
- When the service is full, a new registration puts out the registration, **of any account**,
  that has gone longest without passing a datagram. What is in use stays; what is merely held
  goes first, whoever holds it.

## 7. What the two relays say to each other over ssh

After its greeting the server end sends its `E`. The client end, **once per session**, answers
with the service's address, the term id, the server end's voucher, the pass with every field it
signs, and the term's box public key. Each is written in base64url, since the words between
relays are split at spaces (`src/relay/frame.h`). Old relays ignore words they do not know.

The server end learns the user's account name and pass this way. It needs them to register, and
it is the user's own server.

## 8. Each input: absent, late, wrong, lying

- **Absent.** The service down: no COOKIE; the end's log says "no answer to hello", and it goes
  on with its other paths, asking again (6.2). No pass on the machine: the end does not try, and
  `gosh` says to run `gosh meetup-login`. The other end not registered within 60 seconds: the
  registration is dropped. The web side or a sign-in provider down: nobody can enrol or renew;
  passes already given go on working until their terms expire, and terms go on rolling (the tool
  does not need the web side), so a week's outage costs only the users whose passes expire that
  week.
- **Late.** A cookie older than ten seconds is refused; the end asks for another. An old PASS is
  refused once 1,024 behind. An expired pass gets EXPIRED.
- **Wrong.** Before a good cookie, nothing is answered (but EXPIRED). After one: REFUSED with why,
  or (a box that does not open, a voucher that is not that user's for this service, term and pair)
  dropped. On the web: every check in section 5 that fails says, on the page, which.
- **Lying.** An end for a `P` that is not for it is passed nothing. A user can vouch for any pair
  of keys; it spends their own registrations. A provider that vouches for an address it should not
  (a tenant that sets its users' addresses itself): that is why Microsoft addresses need
  `xms_edov` or a listed tenant, and Google domains need `hd`.
- **The service's own state.** Missing or unreadable at start: it does not start. Unreadable
  later: the old state stays, and it says so.

## 9. What an attacker can do, and what is not closed

| Attacker | Today | Proposed |
| --- | --- | --- |
| Knows nothing of the service | Gets no answer | Gets no answer by UDP (a scanner); the web side is a web page, and gives a term's public key to anyone who asks |
| Knows a term's public key; holds no pass | Uses the meetup for ever with one copied datagram; takes an end's place | Gets cookies; makes the service do one hash a datagram, and a limited number of costly opens; sees that some `E` registers every 25 s, from where. Cannot register, pass, or take a place |
| On the path, can forge addresses; no pass | As above | Can replay only what the service did not receive. Can answer an end first with a false COOKIE, or a false EXPIRED, and so keep it from registering: a denial, which dropping would give anyway |
| Anyone with an account at the three providers | — | Enrols as themselves under the default whitelist: **most people**. A blacklisted person makes a new account in a minute. Behind a narrow whitelist, as a school writes it, neither is so |
| A Microsoft tenant's admin who sets a user's address to someone else's | — | Refused: a Microsoft address is taken only with `xms_edov` or from a listed tenant |
| A former member of a school who still has a verified school address on a personal account | — | Enrols as that address under `id:google:.*@school\.edu` unless `hd` is also required; the design requires it |
| An address that was someone else's, given out again by the school | — | Is a new person: the stable id differs (if section 13, question 1, is yes); otherwise inherits the old holder's blacklist entry and limits |
| Someone who sends a victim an enrolment link for the attacker's own key | — | The victim sees a confirmation page naming their account and four words they have not been shown by any `gosh` of theirs; the paste path warns in large type. Phishing is made harder, not impossible |
| Another local user, or a web page, sending requests to the enrolling machine's loopback | — | Ignored: without the `state` nothing is taken; and a pass is taken only for the machine's own `U` |
| Another user of the service | Takes an end's place, if they see the pair's name | Cannot open another's messages, register as another's end, or be passed another pair's datagrams. Has their own limits; fills the service only as far as the whole service's eviction allows |
| A server host the user logged in to | Holds the shared key: a user of the meetup for ever | For that session, is that session's server end. Learns the user's account name and pass. Afterwards holds a voucher naming an end nobody is: put out after 60 seconds. Cannot relay for itself |
| Someone who steals a user's private key | — | Is that user, until the key is blacklisted or the pass expires. Cannot read or join a running session |
| Someone who takes over a user's account at the provider | — | Can enrol a key of their own as that user, until the account is blacklisted |
| Someone who breaks into the web side | Sees which addresses talk and how much | Can sign passes with the current term's signing key, until an emergency roll. Holds no box key, so reads no registration. Can add to the blacklist, not remove from it |
| Someone who breaks into the UDP side | — | Holds the box keys of the last four terms: can read the registrations sealed to them (which account, at which address, for which peer, each `Kr`) and pose as the service by UDP. Holds no signing key. Not the content of any session |
| Anyone, with huge or malformed datagrams | (the fork never opened what it was sent) | Refused by length before anything is opened; what fails to open is caught |

In every row, nobody reads or changes what the two ends say: that is sealed by the session.

**Not closed, besides what the table says:**

- **No forward secrecy for registrations** (the owner: "ok"); the remedy is an emergency roll.
- **The service is trusted** not to drop, and sees who talks to whom, how much, and as which
  account. It writes none of that to a log (deferred by the owner).
- **A PASSED proves nothing to the end that gets it.** The session refuses what is not its own.
- **An end's key `E` is in the clear** when it registers. mosh's own datagrams carry a counter in
  the clear.
- **A NAT that changes an end's port** costs 3.5 seconds and two round trips to mend.
- **A user's private key is a file** with no passphrase.
- **The pass and the account name reach the user's server** (section 7).
- **The regular expressions are whoever runs the service's to get right.** Full-text matching and
  lower-cased ASCII remove the commonest mistakes, not all.
- **TweetNaCl** is small and slow. The service's CPU limits are to be set from TweetNaCl measured
  on the machine it will run on. Its signature check accepts some signatures in more than one
  form; nothing here is keyed on a signature's bytes.
- **IPv4.** The UDP side listens on IPv4, as today; the answer's address field fits IPv6.
- **Cost.** Each relayed datagram is sealed once more by the sender and opened once by the
  service. This path is the last resort before ssh.

## 10. What changes in gosh, and what is new

**Done already** (groundwork): `vendor/tweetnacl/`, checked against published answers in the
check; `src/meetup/nacl.*`, the one wrapper that calls it. *It does its key agreement twice per
box, once to check for an all-zero secret and once to seal; it is to do it once, and to keep the
agreed key for a registration (section 6.1, step 3).*

**In gosh** (C++, this repository):

- `src/meetup/`: `proof.*` goes. New logic, each with a test of bytes and numbers and no socket:
  `pass.h` (the exact encodings of section 3, made and checked, with the shared test vectors),
  `terms.h` (ids, which are good, which expired), `cookie.h`, `messages.h` (every kind; exact
  lengths), `registrations.h` (6.1, 6.2, 6.4), `passing.h` (6.3, with the rates), `blacklist.h`
  (the plain text blacklist of section 4: read, and matched by exact text). `serve.cc` stays the
  edge: the socket, the time since start, gosh's directory.
- `gosh-meetup term` (and `--now`): the one tool that rolls terms (section 3). Its keys and the
  UDP side's are files in gosh's directory on the machine that runs the service.
- `gosh meetup-login HOST` (and `--paste`): section 5, steps 1, 2, 7 and 8. A reader of one
  posted form on loopback; no TLS.
- The words between relays (section 7).
- `src/pathfinding/`: the end's half, as logic, when that part is reshaped (the rest of queue
  item 8). **In the fork today, found by the first attack:** `punch.cc` hands any probe-marked
  datagram of up to 65,528 bytes to the cipher's `decrypt`, whose 2,048-byte buffer is guarded
  only by an `assert`. Path-finding is off in gosh; this is fixed before it is switched on.
- Tests: the attacks of 2026-11-15 turned round (each thing an onlooker could do becomes a check
  that they cannot), and one for each row of section 9 that a test can reach.
- No private key, `Kr`, pass, voucher, account name or the bytes of a registration is written
  to a log.

**New, in a repository of its own, with requirements of its own:** the web side (section 5): its
SQLite database, its regular expressions, its sign-in. It shares with gosh only the encodings of
section 3, by their test vectors, and the format of the blacklist file.

A gosh meetup and the fork's relays ignore each other; the requirements already say gosh does
not work with the fork's releases at the other end.

## 11. What whoever runs a service does

- **Once:** register the service with each sign-in provider; write the whitelist; run
  `gosh-meetup term` to make the first term; start the two sides under their two users.
- **Every day** (cron): `gosh-meetup term`. On most days it does nothing; it exits non-zero, and
  says why, if anything is wrong.
- **When someone must be put out:** add them to the web side's blacklist, which writes them to
  the UDP side's file; or write the line by hand. They are refused within seconds.
- **After a break-in, or a lost key directory:** `gosh-meetup term --now`. Everyone signs in again.
- **Backups:** the database and the public keys. Never a private key.

## 12. How this proposal got here

- **Revision 1**: one shared key and the clock. Attacked: twelve findings (among them that
  someone with the shared key who saw one registration kept that pair from anywhere; an
  oversized datagram would abort the meetup inside the cipher; a tag over a counter alone let a
  payload be swapped; the clock rule failed silently).
- **Revision 2** took them, with a key per user derived from a master. It kept to a shared-key
  cipher because the agent read `REQ1/SHAPE-3` as ruling out public keys; **that was a mistake:**
  it rules out a library a machine may lack, not source carried here.
- **Revision 3**: key pairs, authorities that sign users' keys by hand. Attacked: eleven findings
  (a voucher bound to nothing let a server host lock a user out for good; every stranger's
  datagram cost a costly step; answers were not tied to their requests; renewing with signatures
  would not fit; silence for every failure).
- **Revision 4** took those.
- **Revision 5**: the owner's direction: one service that signs its own passes, with term keys
  that roll, sign-in with an account the user has. Attacked: thirteen findings. Among them:
  **anyone could have enrolled as anyone**, because the design trusted the provider's address
  without naming which claim or check, and a Microsoft tenant's admin can set any address; a
  pass whose term had passed could not be refused with a reason, since the key to seal the
  refusal was gone; nobody was named to roll terms and the two sides could disagree; the
  loopback listener would take the first request from anyone; a crafted link could enrol an
  attacker's key under a victim's account; two regular-expression engines would disagree and one
  could hang the UDP side; the signing key was not kept from the UDP side; the service could be
  filled and used as a free relay.
- **Revision 6** (this) takes those; SQLite and regular expressions are the web side's, and gosh
  keeps plain text. Four of them touch the owner's
  earlier decisions and are asked in section 13 rather than decided.

## 13. Questions for the owner

1. **A stable id in the pass**, the provider's issuer and subject, beside the account name: so
   that an address the school gives to someone else later is not the same person, for the
   blacklist and the limits. It is not shown and not matched by the lists. (You said no opaque
   id was needed to hide the user, which is right; this is for telling two people apart.) Agreed?
2. **A minimal record of enrolments** on the web side: when, which account, and the four words of
   the key, kept a year. Without it, whoever runs the service can put out a whole person but not
   one lost or phished machine, since nothing says which keys were enrolled for whom. This is
   narrower than the log you deferred, which named who used the service. Agreed?
3. ~~SQLite carried in gosh?~~ Answered: "not needed here, that's requirements for the web
   service." gosh keeps plain text in `~/.config/gosh/` (section 2). **What follows from it, for
   you to confirm:** the UDP side's blacklist is a plain text list of exact account names and
   keys, written by the web side (section 4), not regular expressions. Agreed?
4. **Rates and limits per account, per domain and for the whole service** (6.3, 6.4), set from
   measurement and kept in the database. Agreed in principle?
