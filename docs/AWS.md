# Your own server on AWS, by IPv6

How to set up a small AWS server of your own with an IPv6 address and no IPv4 one, and use it two
ways (requirements v16, *Scope decisions*: "gosh to the aws server direct is #1, gosh to their
home desktop is #2"):

1. **As the machine you work on**: `gosh` straight to it.
2. **As your meetup**: when you work on a machine with no public address, a desktop at home,
   the two ends pass the session through this server, if they can find no way straight to each
   other (`REQ15/PATH-4`).

Building gosh, and putting it where ssh finds it, is [`USING.md`](USING.md), *Setting it up*.

**What has been tried.** Everything gosh does here is tested on one machine, by IPv4 and by IPv6
loopback (`tools/check.sh`). **None of this page has been run on AWS by gosh's tests**, and
neither case has been run between two real machines yet (queue item 8). What is said of AWS below
is from AWS's documentation and pricing as the author knew them, not checked here; if the console
differs, the console is right.

## Why IPv6 only

AWS charges for every public IPv4 address, by the hour (since 2024: about $3.60 a month per
address); an IPv6 address is free. A server with IPv6 alone is the cheaper one for a student to
run (requirements, *Scope decisions*: "ipv6 is important for the server --- ow the server costs
more").

The cost of that: **only machines with IPv6 can reach it.** Campus has IPv6. A laptop on a home or
café network without IPv6 cannot reach the server at all, by gosh or by ssh, and nothing in gosh
can mend that (`REQ17/PATH-8`). If that matters to you, give the server an IPv4 address as well,
and open its ports to IPv4 too (below); gosh then tries IPv4 first and IPv6 if that does not answer.

## 1. The server

In the EC2 console (or the CLI), once:

1. **A VPC with IPv6.** Give the VPC an Amazon-provided IPv6 block, and make a subnet in it that is
   *IPv6-only* (or dual-stack, if you chose an IPv4 address as well). Route `::/0` to the VPC's
   internet gateway.
2. **An instance in that subnet**, with *Auto-assign IPv6 IP* on. IPv6-only subnets need an
   instance type built on Nitro (the `t3`, `t4g` and later families); the smallest is enough for a
   meetup. Choose Ubuntu or Debian, and your ssh key.
3. **A security group** that lets in, from `::/0` (or from the ranges you use):

   | Protocol | Port | For |
   | --- | --- | --- |
   | TCP | 22 | ssh: logging in, starting each session, and the relay when UDP cannot get through |
   | UDP | 60000–61000 | sessions straight to this server (case 1) |
   | UDP | 3478 | the meetup (case 2), or whatever port you give it |

   If you gave the server an IPv4 address as well, add the same three from `0.0.0.0/0`: a session's
   first try is by IPv4, and with UDP closed to IPv4 it would start through ssh, more slowly.
   These ports are open to anyone, but only a datagram sealed with the session's key, which is
   made for each session and passed inside ssh, is acted on; a port is in use only while a session
   holds it.

   With only TCP 22 open, case 1 still works, through the relay over ssh, more slowly.
4. **A name.** Put the server's IPv6 address in DNS as an `AAAA` record (`myserver.example.org`),
   if you can. gosh takes a literal address too, `[2001:db8::1]`, but a name is easier to type,
   and `--meetup` needs a name or an address that the system can look up (an alias in
   `~/.ssh/config` is not one).

From your laptop, check that it answers by IPv6: `ssh -6 ubuntu@myserver.example.org true`.

### Getting gosh onto it

An IPv6-only server cannot reach a site that has no IPv6 address. **github.com had none when this
was written** (`curl -6 -sI https://github.com` says), so `git clone` from the server may fail.
Copy the source from your laptop instead, with its history and the core:

```sh
rsync -a --exclude .build --exclude .deps ~/gosh/ ubuntu@myserver.example.org:gosh/
ssh ubuntu@myserver.example.org 'cd gosh && tools/build.sh'
```

The packages the build needs (`USING.md`, step 1) come from the distribution's archive. If
`sudo apt-get update` cannot reach it by IPv6, AWS's remedy is DNS64 with a NAT gateway, which
costs more than the IPv4 address you saved; the simpler one is a server with an IPv4 address for
the hour it takes to set up.

Then put the programs in `~/.local/bin`, as `USING.md` step 2 says.

## 2. Case 1: working on the server

```sh
gosh ubuntu@myserver.example.org
gosh -L 8000:localhost:8000 ubuntu@myserver.example.org    # and see its web server at localhost:8000
```

That is all. With UDP 60000–61000 open, the session goes straight there by IPv6. With it closed, or
blocked by the network you are on, the session falls back to ssh after 3 seconds and carries on.

## 3. Case 2: the server as your meetup

The meetup is `gosh-server meetup PORT`, left running on the server. It answers the question
"where do I appear to be?" for anyone (at most 200 a second), and passes datagrams between the
two ends of a ticket. A ticket comes from `gosh-server meetup-ticket`, run by ssh: whoever can
log in to the server can have one, and nobody else can use the meetup (`REQ18/PATH-5`). There is
no account and no password of its own.

### Start it

On the server:

```sh
nohup ~/.local/bin/gosh-server meetup 3478 >/dev/null 2>&1 &
( crontab -l 2>/dev/null; echo '@reboot $HOME/.local/bin/gosh-server meetup 3478' ) | crontab -
```

The second line starts it again whenever the server restarts; neither needs root. The first run
makes the master key, `~/.config/gosh/meetup.key`, readable by you alone. Do not copy it
anywhere. To end every ticket made so far: `gosh-server meetup-key --new`, then restart the
meetup.

It listens by IPv4 and by IPv6, each where the server has it, and the two ends may be of
different families.

### Use it

The machine at home must be one you can already reach by ssh from your laptop, somehow: ssh to it
is what starts the session and carries it until a better way is found (requirements, *Scope
decisions*: "any established connection implies relay"). A common way is a tunnel the home
machine keeps open to your server (`ssh -N -R 2222:localhost:22 ubuntu@myserver.example.org`) and
an entry in your laptop's `~/.ssh/config` that goes through it:

```
Host home
    HostName localhost
    Port 2222
    User me
    ProxyJump ubuntu@myserver.example.org
```

Then, from the laptop, with gosh on all three machines:

```sh
gosh --meetup=ubuntu@myserver.example.org:3478 home
```

What happens: the session tries UDP and finds no way, so after 3 seconds it goes over ssh; the
laptop's relay fetches a ticket from the meetup by ssh, once, and hands it to the relay at home;
both register with the meetup and look for a way straight to each other; if they find one they
take it, and if not their datagrams go through the meetup by UDP. Through the meetup is still
"fallen back": the ssh connection stays open in case the meetup stops answering. The relay's log
(`GOSH_RELAY_LOG=FILE`) says each step: "a ticket for the meetup at ...", "registered with the
meetup at ...".

**Not tried yet:** this case between real machines, with the tunnel above (queue item 8, "two
machines in one room"). How gosh finds the home machine's address through `ProxyJump` has not been
checked either; if the session does not come up, the relay's log says how far it got. (Not
`--relay=always`: that means ssh and nothing by UDP, so no meetup either.)

**A home machine with no IPv6** cannot reach an IPv6-only meetup, so this case needs either IPv6
at home or a server that keeps an IPv4 address (requirements, *Scope decisions*: "say and leave
it").

### What the meetup can and cannot do

It never holds a session's key, so it cannot read or change what it passes; it can drop it, which
stops the session moving through it, and the session stays on ssh. It sees which two addresses
talk, and how much, and writes none of it down. A ticket is good for 30 days, for whoever holds
it. The design and what it leaves open: `docs/PROPOSAL_meetup-registration.md` in the repository.
