#!/usr/bin/env python3
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""REQ15/PATH-4, REQ15/PATH-5: a session that falls back to the relay gets a ticket, and both relays register with the meetup.

usage: relay-meetup.py SERVER CLIENT [ADDRESS]

ADDRESS is where the session and the meetup run: 127.0.0.1 by default, or ::1 for both by IPv6
(REQ16/PATH-8).

On loopback: a session whose client is given a UDP port where nobody is, so that it falls
back to the relay (run directly, in place of ssh); a meetup, "SERVER meetup PORT"; and the
client told of the meetup, with the command that fetches its ticket run directly too (in
place of "ssh MEETUP gosh-server meetup-ticket"). HOME and XDG_CONFIG_HOME are a directory of
the test's own, so the meetup's master key is nobody's real one.

  the local relay   fetches a ticket, and registers with the meetup
  the remote relay  is given the ticket over the relay, and registers too
  the session       works throughout: a TCP forward, there and back

On one machine the two relays also find a way straight to each other, and prefer it, so this
does not show datagrams going through the meetup: src/pathfinding/punch-test.cc does that, with
the meetup's own logic. It stops only what it started, by process id.
"""
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from variables import both

server, client_path = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
address = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
family = socket.AF_INET6 if ":" in address else socket.AF_INET
meetup_text = ("[%s]" % address) if family == socket.AF_INET6 else address
failures = []


def check(name, ok, detail=""):
    print("%-72s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail), flush=True)
    if not ok:
        failures.append(name)


def free_port(kind=socket.SOCK_DGRAM):
    s = socket.socket(family, kind)
    s.bind((address, 0))
    port = s.getsockname()[1]
    s.close()
    return port


target = socket.socket()
target.bind(("127.0.0.1", 0))
target.listen(4)


def echo_loop():
    while True:
        try:
            c, _ = target.accept()
        except OSError:
            return

        def echo(c=c):
            try:
                while True:
                    d = c.recv(65536)
                    if not d:
                        break
                    c.sendall(d)
            except OSError:
                pass
            c.close()
        threading.Thread(target=echo, daemon=True).start()


threading.Thread(target=echo_loop, daemon=True).start()
forward_port = free_port(socket.SOCK_STREAM)
spec = "%d:127.0.0.1:%d" % (forward_port, target.getsockname()[1])

home = tempfile.mkdtemp(prefix="gosh-relay-meetup-")
handle, relay_log = tempfile.mkstemp(prefix="gosh-relay-log-")
os.close(handle)
env = both(dict(os.environ, LC_ALL="C.UTF-8", HOME=home, XDG_CONFIG_HOME=os.path.join(home, "config"), MOSH_STUN="none"))
started = []
session_pid = None
try:
    meetup_port = free_port()
    meetup = subprocess.Popen([server, "meetup", str(meetup_port)], env=env, stdin=subprocess.DEVNULL)
    started.append(meetup)
    time.sleep(0.3)

    up = subprocess.run([server, "new", "-i", address, "-L", spec, "--", "/bin/sh", "-c", "while :; do sleep 1000; done"],
                        env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
    match = re.search(r"MOSH CONNECT (\d+) (\S+)", up.stdout)
    pid = re.search(r"pid = (\d+)", up.stderr)
    session_pid = int(pid.group(1)) if pid else None
    check("the server starts", bool(match), (up.stdout + up.stderr)[-300:])
    if not match:
        sys.exit(1)

    nobody = socket.socket(family, socket.SOCK_DGRAM)
    nobody.bind((address, 0))  # bound and never read: datagrams sent here vanish
    client_env = dict(env, MOSH_KEY=match.group(2), MOSH_RELAY="auto", MOSH_RELAY_LOG=relay_log,
                      MOSH_RELAY_COMMAND="\x1f".join([server, "relay", address, match.group(1)]),
                      MOSH_MEETUP="%s:%d" % (meetup_text, meetup_port),
                      MOSH_MEETUP_TICKET_COMMAND="\x1f".join([server, "meetup-ticket"]))
    client = subprocess.Popen([client_path, "-N", "-L", spec, address, str(nobody.getsockname()[1])],
                              env=both(client_env), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                              stderr=subprocess.PIPE)
    started.append(client)
    said = []
    threading.Thread(target=lambda: said.extend(l.decode("latin-1") for l in client.stderr), daemon=True).start()

    def logged():
        with open(relay_log) as f:
            return f.read()

    deadline = time.time() + 40
    while time.time() < deadline and not any("connected; forwarding" in l for l in said):
        time.sleep(0.1)
    check("the session comes up, by the relay", any("connected; forwarding" in l for l in said), "".join(said)[-300:])

    deadline = time.time() + 20
    while time.time() < deadline and logged().count("registered with the meetup at %s:%d" % (meetup_text, meetup_port)) < 2:
        time.sleep(0.2)
    log = logged()
    check("the local relay fetches a ticket", "local: a ticket for the meetup at %s:%d" % (meetup_text, meetup_port) in log, log[-600:])
    check("and registers with the meetup", "local: registered with the meetup at %s:%d" % (meetup_text, meetup_port) in log, log[-600:])
    check("the remote relay, given the ticket, registers too",
          "remote: registered with the meetup at %s:%d" % (meetup_text, meetup_port) in log, log[-600:])

    try:
        c = socket.create_connection(("127.0.0.1", forward_port), timeout=10)
        c.settimeout(10)
        c.sendall(b"with a meetup about")
        got = c.recv(100)
        c.close()
    except OSError as e:
        got = repr(e).encode()
    check("and the session works: a TCP forward, there and back", got == b"with a meetup about", got)
finally:
    for p in reversed(started):
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
    if session_pid:
        try:
            os.kill(session_pid, 15)
        except OSError:
            pass
    shutil.rmtree(home, ignore_errors=True)
    os.remove(relay_log)

if failures:
    sys.exit("FAIL REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: %s" % "; ".join(failures))
print("PASS REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: a relay that falls back fetches a ticket, and both relays register")
