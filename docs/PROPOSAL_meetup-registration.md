# Proposal: the meetup, on the student's own server, with ssh as its only authority

**A proposal, not the design in force. Revision 10, 2026-11-15.** Nothing here is built. The
requirements it is written to are `REQUIREMENTS_v15.md`, accepted the same day.

The owner, after six attacks on revisions 1 to 9: "ok, this got more complicated than it needed
to be. ... lets just get back to the basics: any esablished connection implies relay. we already
defer to ssh, let's do it again, the server supports stun and meetup, no third party. the point
was to have students install their own on an aws account. no new auth." And then: the meetup is a
different machine from the session's server, "at least that's what this should allow / gosh to
the aws server direct is #1, gosh to their home desktop is #2"; public STUN servers off by
default, "yes"; TweetNaCl removed, "yes".

What revisions 1 to 9 said, and what each attack found, is in this file's history in git
(revision 9 is commit `c507e97`). What this revision keeps from them is named where it is used.

## 1. What is wrong today

A meetup has one key, shared by everyone allowed to use it. To have a datagram passed on, an
end puts twelve bytes in front of it: four random bytes and a tag of those four bytes. They
travel in the clear, say nothing of who sent them, when, or for which pair, and the meetup
treats whoever last named a pair as one of its ends. So anyone who sees one datagram can use
the meetup for ever and can take an end's place (`src/meetup/README.md`; `passing-test.cc`,
`test_replay`, with the real cipher).

## 2. The two cases, and the shape

1. **A laptop to the student's AWS server.** The server has a public address: the session goes
   by UDP, and by the relay inside ssh when UDP cannot get through. No meetup is needed.
2. **A laptop to the student's desktop at home**, behind its NAT. ssh reaches it however the
   student already reaches it (`ssh -J aws home`, or a tunnel the desktop keeps open to the AWS
   server): "any established connection implies relay", so the session starts on the relay.
   Then path-finding asks **the AWS server** where each end appears to be, and the two ends try
   to punch through. If they cannot, **the AWS server passes their datagrams** as a meetup.

So the student's AWS server, running `gosh-server meetup`, is their STUN server and their meetup.
Nobody else is asked anything: "no third party".

**ssh is the only authority.** Whoever can log in to the AWS server as the student may use the
student's meetup, and only by what they are given over that login. There is no sign-in, no
account and no list of users: "no new auth".

## 3. Tickets

The meetup keeps one secret, **its master key**: 16 random bytes in a file in gosh's directory
(`~/.config/gosh/meetup.key`), readable only by the student, made the first time
`gosh-server meetup` runs.

A **ticket** is what lets one session use the meetup. It is:

```
ticket id    16 bytes: 8 random, then the instant it stops being good (UTC seconds, 8)
ticket key   16 bytes: the first 16 bytes of an HMAC, under the master key, of the ticket id
```

`gosh-server meetup-ticket`, run over ssh on the AWS server, makes one and prints it
(`gosh-ticket ID KEY`); the port is the one `--meetup=HOST:PORT` names. So the meetup keeps nothing per ticket: it computes the key again from the id
whenever it needs it. *(Built: the key is mosh's own cipher, AES-OCB, sealing the id under the
master key with a fixed nonce, which for one block is a keyed permutation; `src/meetup/ticket.h`
says why that is safe. Nothing new is carried in.)*

1. When a session has fallen back and path-finding starts, the client end runs
   `ssh AWS gosh-server meetup-ticket` (the AWS server is the one named by `--meetup=HOST`), and
   gets a ticket good for 30 days.
2. It sends the ticket to the server end over the relay, inside ssh, as it already sends the
   path-finding key.
3. Each end registers with the meetup under the ticket, one as end 0 and the other as end 1.

The ticket key crosses the network only inside ssh. A ticket is for one session's two ends; it
says nothing of the session's own key, which the meetup never holds.

**To end every ticket at once** (a ticket key leaked; the student's laptop stolen), the student
runs `gosh-server meetup-key --new` and starts the meetup again: a new master key, and every
ticket made under the old one is no good. Sessions then fetch a new ticket the next time they fall back.

## 4. Registering and passing

UDP to the meetup's port. Each kind of datagram begins with a mark of its own (8 bytes) and a
version (1), and has one exact length, but PASS and PASSED. **Lengths are checked before anything
is opened**, the cipher's included (it guards its buffers with an `assert` that some builds leave
out). "Sealed" is mosh's own cipher (AES-OCB) under the ticket key, with the counter as its nonce;
end 0's and end 1's counters, and the meetup's, are told apart by two bits.

```
end -> meetup   REGISTER    mark | version | ticket id (16) | end (1) | counter (8) |
                            sealed: { kind, where the end believes it is: family, address, port }
meetup -> end   REGISTERED  mark | version | ticket id (16) | end (1) | counter (8) |
                            sealed: { kind, where the end was seen: family, address, port }
end -> meetup   PASS        mark | version | ticket id (16) | end (1) | counter (8) | sealed: { the datagram }
meetup -> end   PASSED      mark | version | the datagram
```

- **REGISTER.** The meetup checks the length; that the ticket has not run out; computes the
  ticket key; opens the seal; checks the counter is above any it has seen for this ticket's end;
  and **checks that the address the end believes it is at is the one the datagram came from**.
  Then it records the end at that address. A REGISTER copied off the network and sent from
  anywhere else is refused, and one sent again from the same place changes nothing. (The sixth
  attack's first finding was a copied registration moved to another address; this is why the
  address is inside the seal.) An end learns where it appears to be by asking the meetup's STUN
  answer first.
- **REGISTERED** is the same length as REGISTER, so the meetup sends nothing larger than it was
  sent.
- **PASS.** In this order: the length; the ticket's end is registered; the datagram came from its
  recorded address; the counter is inside a window of 1,024 and not used; the seal opens. Then
  the datagram goes, as PASSED, to the ticket's other end, at its recorded address, if that end
  registered within two minutes. At most 1,500 bytes of datagram; a session's are about 1,300.
- **Keeping it.** An end sends REGISTER again every 25 seconds while it uses the meetup, and at
  once when its address may have changed (a REGISTERED reporting another address, or 3.5 seconds
  with nothing passed back while it sends). A registration not renewed for two minutes is
  dropped.
- **How many.** At most 64 tickets' registrations at once: it is one student's meetup. When full,
  the one that has gone longest without passing a datagram is put out.
- **What is answered to whom.** Nothing that fails a check is answered. STUN is answered to anyone
  (section 5).

All of it is cheap: one MAC to compute a ticket key, one seal to open. No public-key work, so
nothing an outsider can make expensive.

## 5. The STUN answer

`gosh-server meetup` answers the STUN question on its port, to anyone, as the fork's did (no more
than 200 a second, and never more bytes than it was sent: `src/meetup/answer.h`). Path-finding
asks the meetup named by `--meetup=HOST`, and no public STUN server unless `--stun=HOST` names
one. With neither, path-finding finds only addresses on the local network.

## 6. What it guards against, and what is not closed

A meetup never holds a session's key: what two ends say is sealed by mosh with a key exchanged
over ssh. So the meetup guards against **outsiders**:

| Attacker | Today | Proposed |
| --- | --- | --- |
| Knows the meetup's address | Uses it for ever with one copied datagram; takes an end's place | Gets STUN answers. Cannot register, pass, or take a place: every one needs a ticket key |
| On the path; sees a session's datagrams to the meetup | As above | Cannot move a registration (the address is sealed in it), replay a PASS (counters), or forge one. Can drop: a denial, which being on the path gives anyway |
| Wants to aim the meetup at someone else | — | Gets back no more bytes than it sends, and only STUN, rate-limited |
| The session's server end (the home desktop) | Holds the shared key: the meetup for ever | Holds the session's ticket for 30 days: can pass datagrams through the student's own meetup until then. It is the student's own machine |
| Someone who can log in to the AWS server as the student | — | Is the student: can make tickets, and read the master key. Out of scope: ssh decides who that is |

**Not closed:**
- **A ticket lasts 30 days**, whoever holds it; the remedy for one that leaks is a new master key.
- **The meetup sees** which two addresses talk, and how much. It writes none of that down.
- **An end's ticket id and counter are in the clear.**
- **A NAT that changes an end's port** costs up to 3.5 seconds to mend.
- **IPv4 only**, as today.
- **Cost:** each passed datagram is sealed once more by the sender and opened once by the meetup.
  This path is the last resort before ssh.

## 7. What changes in gosh

- `src/meetup/`: `proof.*` goes. New logic, each with a test of bytes and numbers and no socket:
  `ticket.h` (the id, the key from the master key, running out), `messages.h` (every kind, at its
  exact length), `registrations.h` (REGISTER, keeping, eviction), `passing.h` (PASS). `answer.h`
  stays. `serve.cc` stays the edge: the socket, the clock, the master key's file.
- `gosh-server meetup-ticket`, and `gosh-server meetup-key --new` in place of today's
  `meetup-key`.
- The client end fetches a ticket over ssh when path-finding starts and sends it to the server end
  over the relay. `MOSH_MEETUP_KEY` and the fork's twelve-byte proof go.
- Path-finding's default STUN servers go: `stun.l.google.com` and `stun.cloudflare.com` are asked
  only if named (`src/relay/settings.h`).
- **TweetNaCl goes**: `vendor/tweetnacl/` and `src/meetup/nacl.*`, carried for revisions 5 to 9.
- `src/pathfinding/`: the end's half, as logic, when that part is reshaped (the rest of queue
  item 8). **In the fork today, found by the first attack:** `punch.cc` hands any probe-marked
  datagram of up to 65,528 bytes to the cipher's `decrypt`, whose 2,048-byte buffer is guarded
  only by an `assert`. Path-finding is off in gosh; this is fixed before it is switched on.
- Tests: the fork's replay (`passing-test.cc`, `test_replay`) turned round, so that it shows a
  copied datagram refused; one for each row of section 6 that a test can reach.
- No ticket key, master key, or the bytes of a registration is written to a log.

## 8. What the student does

Once, on the AWS server: install gosh; open one UDP port in the instance's security group; start
`gosh-server meetup PORT` (from cron at reboot, or a user service). Then, from the laptop:
`gosh --meetup=AWS home`, where `home` is however ssh already reaches the desktop.
