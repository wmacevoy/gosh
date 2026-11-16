#!/usr/bin/env python3
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""REQ1/PATH-1: a datagram from a stranger is not the server answering.

usage: relay-spoof.py SERVER CLIENT

UDP between the client and the server is blocked, so the client starts its
relay and the session is carried by the relay's stream. The local relay goes
on sending by UDP too, and takes an answer by UDP to mean the direct way
works, at which it stops using the stream. This test is a stranger on the
path: it sees where the relay's datagrams come from and sends a datagram of
its own back there, from an address that is not the server's, five times a
second.

  pf.7 and the fork: any datagram arriving at that socket counts as the
  server's. The relay stops using the stream, nothing gets through by UDP,
  and the session stops carrying anything (FINDINGS.md, 2026-11-14, the
  second of five).

  gosh: only a datagram from the server's own address counts. A byte sent
  through a forward still comes back.

Written for gosh on 2026-11-15, and seen red that day against pf.7 and
against gosh before the relay checked where a datagram came from. It asks no
server on the Internet anything, and stops only what it started, by process id.
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
    print("%-72s %s %s" % (name, "ok" if ok else "FAIL", detail), flush=True)
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
    finally:
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

# Where the client is told the server is: a socket of this test's, where datagrams vanish (UDP is
# blocked) but are seen. Whoever sends here after the relay has started is the relay.
blocked = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
blocked.bind(("127.0.0.1", 0))
stranger = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # (not the server's address: another port)
stranger.bind(("127.0.0.1", 0))
relay_started = [False]
relay_at = set()
spoofed = [0]
over = [False]


def watch():
    while not over[0]:
        try:
            _, sender = blocked.recvfrom(65536)
        except OSError:
            return
        if relay_started[0]:
            relay_at.add(sender)


def spoof():
    while not over[0]:
        for where in list(relay_at):
            try:
                stranger.sendto(b"not the server", where)
                spoofed[0] += 1
            except OSError:
                pass
        time.sleep(0.2)


threading.Thread(target=watch, daemon=True).start()
threading.Thread(target=spoof, daemon=True).start()

client_env = dict(env, MOSH_KEY=match.group(2), MOSH_RELAY="auto",
                  MOSH_RELAY_COMMAND="\x1f".join([server_path, "relay", "127.0.0.1", match.group(1)]))
client = subprocess.Popen([client_path, "-N", "-L", spec, "127.0.0.1", str(blocked.getsockname()[1])],
                          env=both(client_env), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
said = []


def listen():
    for line in client.stderr:
        said.append(line.decode("latin-1"))
        if "through ssh" in said[-1]:
            relay_started[0] = True


threading.Thread(target=listen, daemon=True).start()
try:
    deadline = time.time() + 40
    while time.time() < deadline and not any("connected; forwarding" in l for l in said) and client.poll() is None:
        time.sleep(0.1)
    check("UDP blocked: the client starts its relay, and the session comes up", any("through ssh" in l for l in said)
          and any("connected; forwarding" in l for l in said), repr(said[-3:]))

    # Give the stranger time: longer than the 2.5 s after which a relay that thought the direct way
    # worked would have noticed otherwise, had the stranger not been there.
    time.sleep(6)
    check("(the stranger found the relay's socket and sent to it)", spoofed[0] >= 10, "sent %d" % spoofed[0])

    answered = b""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=10) as conn:
            conn.settimeout(15)
            conn.sendall(b"still there?")
            while len(answered) < 12:
                more = conn.recv(64)
                if not more:
                    break
                answered += more
    except OSError as e:
        answered = b"(%s)" % str(e).encode()
    check("with a stranger answering in the server's place, a forward still carries bytes", answered == b"still there?",
          repr(answered))
finally:
    over[0] = True
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
    blocked.close()

if failures:
    sys.exit("FAIL REQ1/PATH-1: %s" % "; ".join(failures))
print("PASS REQ1/PATH-1: a datagram from a stranger is not taken for the server answering")
