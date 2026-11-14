#!/usr/bin/env python3
"""REQ1/FWD-3: a forwarded TCP connection survives a loss of contact.

usage: outage.py SERVER CLIENT

Starts SERVER and "CLIENT -N -L" on loopback with every datagram between them
passing through a gate this test can shut. With a connection open through the
forward it shuts the gate for OUTAGE seconds, writes to the connection while
nothing can get through, opens the gate, and passes if what was written during
the outage arrives, and the same connection still carries bytes afterwards.

The fork tested this only from inside (tcp-forward: six simulated hours with no
socket). This is the test from outside that a part must pass before and after
it is reshaped (queue item 4). It cannot wait six hours; OUTAGE is chosen to be
well past the 10 seconds after which the fork counts the other end as out of
contact, which is where its handling of a connection changes.

Written for gosh on 2026-11-14, and seen red that day: with the gate never
opened again, the last two checks fail. It stops only the processes it started, by
process id, and asks nothing of any server on the Internet.

Copyright 2026 Warren D. MacEvoy. Part of gosh, under the GNU General Public
License, version 3 or later (COPYING).
"""
import os
import re
import signal
import socket
import subprocess
import sys
import threading
import time

OUTAGE = 25      # seconds with nothing getting through; contact counts as lost after 10
PATIENCE = 30    # how long anything else may take before the test gives up waiting

if len(sys.argv) != 3:
    sys.exit(__doc__)
server_path, client_path = sys.argv[1], sys.argv[2]
failures = []


def check(name, ok, more=""):
    print("%-62s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else more), flush=True)
    if not ok:
        failures.append(name)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


# What the forward leads to: it sends back whatever it gets.
target = socket.socket()
target.bind(("127.0.0.1", 0))
target.listen(1)


def echo():
    conn, _ = target.accept()
    try:
        while True:
            data = conn.recv(65536)
            if not data:
                break
            conn.sendall(data)
    except OSError:
        pass


threading.Thread(target=echo, daemon=True).start()

listen_port = free_port()
spec = "%d:127.0.0.1:%d" % (listen_port, target.getsockname()[1])
# No fallback to ssh and nobody asked where we are: the outage is to be waited out, not gone around.
env = dict(os.environ, LC_ALL="C.UTF-8", MOSH_STUN="none", MOSH_PUNCH="never", MOSH_RELAY="never")
env.pop("MOSH_RELAY_COMMAND", None)

started = subprocess.run(
    [server_path, "new", "-i", "127.0.0.1", "-L", spec, "--", "/bin/sh", "-c", "while :; do sleep 1000; done"],
    env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=PATIENCE)
connect = re.search(r"MOSH CONNECT (\d+) (\S+)", started.stdout)
server_pid = re.search(r"pid = (\d+)", started.stderr)
if not connect or not server_pid:
    sys.exit("FAIL REQ1/FWD-3: the server did not start a session with a forward:\n%s%s" % (started.stdout, started.stderr))
server_at = ("127.0.0.1", int(connect.group(1)))

# The gate: the client is told the gate's port, and the gate passes datagrams on to the
# server and back, while it is open.
gate_open = [True]
client_at = [None]
gate = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
gate.bind(("127.0.0.1", 0))
onward = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
onward.bind(("127.0.0.1", 0))


def inward():
    while True:
        try:
            datagram, client_at[0] = gate.recvfrom(65536)
            if gate_open[0]:
                onward.sendto(datagram, server_at)
        except OSError:
            return


def outward():
    while True:
        try:
            datagram, _ = onward.recvfrom(65536)
            if gate_open[0] and client_at[0]:
                gate.sendto(datagram, client_at[0])
        except OSError:
            return


threading.Thread(target=inward, daemon=True).start()
threading.Thread(target=outward, daemon=True).start()

client = subprocess.Popen([client_path, "-N", "-L", spec, "127.0.0.1", str(gate.getsockname()[1])],
                          env=dict(env, MOSH_KEY=connect.group(2)),
                          stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def read_exactly(conn, count, seconds):
    """count bytes from conn, or fewer if they do not come within seconds."""
    conn.settimeout(seconds)
    got = b""
    try:
        while len(got) < count:
            data = conn.recv(count - len(got))
            if not data:
                break
            got += data
    except OSError:
        pass
    return got


try:
    up = False
    for line in client.stderr:
        if b"connected; forwarding" in line:
            up = True
            break
    check("the session and its forward come up", up)
    if up:
        conn = socket.create_connection(("127.0.0.1", listen_port), timeout=PATIENCE)
        conn.sendall(b"before the outage")
        check("the connection carries bytes there and back",
              read_exactly(conn, 17, PATIENCE) == b"before the outage")

        gate_open[0] = False
        conn.sendall(b"written during the outage")
        nothing = read_exactly(conn, 1, OUTAGE)  # this is also the wait
        check("nothing gets through while the gate is shut", nothing == b"", repr(nothing))

        gate_open[0] = True
        back = read_exactly(conn, 25, PATIENCE)
        check("after %d s out of contact, what was written arrives" % OUTAGE,
              back == b"written during the outage", repr(back))
        conn.sendall(b"and after")
        check("and the same connection goes on carrying bytes",
              read_exactly(conn, 9, PATIENCE) == b"and after")
        conn.close()
finally:
    client.terminate()
    try:
        client.wait(PATIENCE)
    except subprocess.TimeoutExpired:
        client.kill()
    try:
        os.kill(int(server_pid.group(1)), signal.SIGTERM)
    except OSError:
        pass

if failures:
    sys.exit("FAIL REQ1/FWD-3: %s" % "; ".join(failures))
print("PASS REQ1/FWD-3: a forwarded connection outlived %d s out of contact" % OUTAGE)
