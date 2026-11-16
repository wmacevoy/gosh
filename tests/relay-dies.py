#!/usr/bin/env python3
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""REQ1/PATH-1: a local relay that dies is noticed, and another is started.

usage: relay-dies.py SERVER CLIENT

The session is carried by the relay from the start, and by nothing else. The
local relay is a process of its own, started by the client; if it dies (it is
killed here, as a bug or the machine's shortage of memory might kill it) the
client is left sending to a port nobody has.

  pf.7 and the fork: nobody notices. The session carries nothing from then on
  and never says so (FINDINGS.md, 2026-11-13).

  gosh: the client sees that its relay has gone, says so, and starts another;
  a byte sent through a forward comes back. Twice, to show it was not luck.

Written for gosh on 2026-11-15, and seen red that day against pf.7 and against
gosh before the client looked. It asks no server on the Internet anything. The
relay it kills is the child of the client it started, found by that and by
process id, and it stops nothing else. (It reads /proc, so it is for Linux.)
"""
import os
import re
import signal
import socket
import subprocess
import sys
import threading
import time
from variables import both

if len(sys.argv) != 3:
    sys.exit(__doc__)
server_path, client_path = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
failures = []


def check(name, ok, detail=""):
    print("%-72s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail), flush=True)
    if not ok:
        failures.append(name)


# what is forwarded to: it sends back whatever it gets
target = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
target.bind(("127.0.0.1", 0))
target.listen(4)


def echo(conn):
    try:
        while True:
            data = conn.recv(65536)
            if not data:
                break
            conn.sendall(data)
    except OSError:
        pass
    conn.close()


def accept_loop():
    while True:
        try:
            conn, _ = target.accept()
        except OSError:
            return
        threading.Thread(target=echo, args=(conn,), daemon=True).start()


threading.Thread(target=accept_loop, daemon=True).start()
probe = socket.socket()
probe.bind(("127.0.0.1", 0))
port = probe.getsockname()[1]
probe.close()
spec = "%d:127.0.0.1:%d" % (port, target.getsockname()[1])

env = both(dict(os.environ, LC_ALL="C.UTF-8", MOSH_STUN="none", MOSH_PUNCH="never"))
started = subprocess.run([server_path, "new", "-i", "127.0.0.1", "-L", spec, "--", "/bin/sh", "-c",
                          "while :; do sleep 1000; done"],
                         env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
match = re.search(r"MOSH CONNECT (\d+) (\S+)", started.stdout)
server_pid = re.search(r"pid = (\d+)", started.stderr)
if not match or not server_pid:
    sys.exit("FAIL REQ1/PATH-1: the server did not start:\n%s%s" % (started.stdout, started.stderr))

client_env = dict(env, MOSH_KEY=match.group(2), MOSH_RELAY="always",
                  MOSH_RELAY_COMMAND="\x1f".join([server_path, "relay", "127.0.0.1", match.group(1)]))
client = subprocess.Popen([client_path, "-N", "-L", spec, "127.0.0.1", match.group(1)],
                          env=both(client_env), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
said = []
threading.Thread(target=lambda: said.extend(l.decode("latin-1") for l in client.stderr), daemon=True).start()


def local_relay():
    """The process id of the client's local relay: its one child that runs the client's own program."""
    found = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % name) as f:
                parent = int(f.read().rsplit(")", 1)[1].split()[1])
            if parent == client.pid and os.readlink("/proc/%s/exe" % name) == os.readlink("/proc/%d/exe" % client.pid):
                found.append(int(name))
        except (OSError, ValueError, IndexError):
            pass
    return found[0] if len(found) == 1 else None


def carried(what):
    """What comes back when these bytes are sent through the forward."""
    answered = b""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=10) as conn:
            conn.settimeout(20)
            conn.sendall(what)
            while len(answered) < len(what):
                more = conn.recv(64)
                if not more:
                    break
                answered += more
    except OSError as e:
        answered = b"(%s)" % str(e).encode()
    return answered


try:
    deadline = time.time() + 40
    while time.time() < deadline and not any("connected; forwarding" in l for l in said) and client.poll() is None:
        time.sleep(0.1)
    check("the session comes up through the relay", any("connected; forwarding" in l for l in said), repr(said[-3:]))
    check("and carries bytes through a forward", carried(b"before") == b"before")

    for which in ("the relay is killed", "the next relay is killed too"):
        relay = local_relay()
        check("(the client has one local relay: its child, by process id)", relay is not None)
        if relay is None:
            break
        os.kill(relay, signal.SIGKILL)
        answered = carried(b"after it died")
        check("%s: a forward carries bytes all the same" % which, answered == b"after it died", repr(answered))
        check("  by another relay, not the dead one", local_relay() not in (None, relay))
    check("the client said that its relay had stopped", any("relay" in l and "stopped" in l for l in said),
          repr(said[-4:]))
finally:
    if client.poll() is None:
        client.terminate()
        try:
            client.wait(timeout=10)
        except subprocess.TimeoutExpired:
            client.kill()
    try:
        os.kill(int(server_pid.group(1)), signal.SIGTERM)
    except OSError:
        pass

if failures:
    sys.exit("FAIL REQ1/PATH-1: %s" % "; ".join(failures))
print("PASS REQ1/PATH-1: a local relay that dies is noticed, and another is started")
