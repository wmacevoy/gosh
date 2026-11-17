#!/usr/bin/env python3
"""REQ19/FWD-6: -W's connection survives a loss of contact, as a forward's does (REQ1/FWD-3).

usage: connect-outage.py SERVER CLIENT

What "gosh -W HOST:PORT host" starts, started here by hand: SERVER with a stream whose end there
is "@connect 127.0.0.1 PORT", and CLIENT with the stream's end here on its own standard input and
output ("@stdio"), every datagram between them passing through a gate this test can shut. PORT
is a target that sends back what it gets: what ssh's server would be, as far as the bytes go
(this test has no ssh server; tests/gosh-command.py runs the command itself, through fake-ssh).

With bytes there and back, it shuts the gate for OUTAGE seconds, writes on the client's input
while nothing can get through, opens the gate, and passes if what was written arrives, the same
connection goes on, nothing else ever appears on the client's output, and when the client's
input ends, the target hears the end and the client finishes.

Written 2026-11-17, and seen red: with @connect not taken by the server, the first check fails;
with the gate never opened again, the later checks fail. It stops only the processes it started,
by process id, and asks nothing of any server on the Internet.

Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""
import os
import re
import select
import signal
import socket
import subprocess
import sys
import threading
import time
from variables import both

OUTAGE = 15      # seconds with nothing getting through; contact counts as lost after 10
PATIENCE = 30    # how long anything else may take before the test gives up waiting
NEVER_OPEN = os.environ.get("CONNECT_OUTAGE_NEVER_OPEN") == "1"  # (to see it red)

if len(sys.argv) != 3:
    sys.exit(__doc__)
server_path, client_path = sys.argv[1], sys.argv[2]
failures = []


def check(name, ok, more=""):
    print("%-66s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else more), flush=True)
    if not ok:
        failures.append(name)


# The target: sends back what it gets; on the end of its input, says so and ends its own.
target = socket.socket()
target.bind(("127.0.0.1", 0))
target.listen(1)
heard_end = threading.Event()


def echo():
    conn, _ = target.accept()
    try:
        while True:
            data = conn.recv(65536)
            if not data:
                heard_end.set()
                conn.sendall(b"[end heard]")
                conn.shutdown(socket.SHUT_WR)
                break
            conn.sendall(data)
    except OSError:
        pass


threading.Thread(target=echo, daemon=True).start()

env = both(dict(os.environ, LC_ALL="C.UTF-8", MOSH_STUN="none", MOSH_PUNCH="never", MOSH_RELAY="never"))
env.pop("MOSH_RELAY_COMMAND", None)
env.pop("GOSH_RELAY_COMMAND", None)

words = "\x1f".join(["@connect", "127.0.0.1", str(target.getsockname()[1])])
started = subprocess.run(
    [server_path, "new", "-i", "127.0.0.1", "-X", words, "--", "/bin/sh", "-c", "while :; do sleep 1000; done"],
    env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=PATIENCE)
connect = re.search(r"MOSH CONNECT (\d+) (\S+)", started.stdout)
server_pid = re.search(r"pid = (\d+)", started.stderr)
if not connect or not server_pid:
    sys.exit("FAIL REQ19/FWD-6: the server did not start a session with a stream:\n%s%s" % (started.stdout, started.stderr))
server_at = ("127.0.0.1", int(connect.group(1)))

# The gate, as in outage.py.
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

client = subprocess.Popen([client_path, "-X", "@stdio", "127.0.0.1", str(gate.getsockname()[1])],
                          env=dict(env, MOSH_KEY=connect.group(2)),
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
os.set_blocking(client.stdout.fileno(), False)
out = [b""]


def read_out(count, seconds):
    """Reads the client's output until it holds count more bytes than were taken, or seconds pass; returns them."""
    deadline = time.monotonic() + seconds  # a limit on waiting, not something asserted
    while len(out[0]) < count and time.monotonic() < deadline:
        ready, _, _ = select.select([client.stdout], [], [], 0.2)
        if ready:
            data = client.stdout.read()
            if not data:
                break
            out[0] += data
    got, out[0] = out[0][:count], out[0][count:]
    return got


def write_in(data):
    client.stdin.write(data)
    client.stdin.flush()


try:
    write_in(b"before the outage")
    got = read_out(17, PATIENCE)
    check("the connection carries bytes there and back", got == b"before the outage", repr(got))

    if not failures:
        gate_open[0] = False
        write_in(b"written during the outage")
        nothing = read_out(1, OUTAGE)  # this is also the wait
        check("nothing gets through while the gate is shut", nothing == b"", repr(nothing))

        gate_open[0] = not NEVER_OPEN
        back = read_out(25, PATIENCE)
        check("after %d s out of contact, what was written arrives" % OUTAGE,
              back == b"written during the outage", repr(back))
        write_in(b"and after")
        got = read_out(9, PATIENCE)
        check("and the same connection goes on carrying bytes", got == b"and after", repr(got))

        client.stdin.close()  # as ssh does when it is done with its ProxyCommand
        check("when the client's input ends, the target hears the end", heard_end.wait(PATIENCE))
        got = read_out(11, PATIENCE)
        check("and what the target says after that still arrives", got == b"[end heard]", repr(got))
        try:
            status = client.wait(PATIENCE)
        except subprocess.TimeoutExpired:
            status = None
        check("and then the client finishes", status == 0, repr(status))
        rest = read_out(1, 1)
        check("nothing else was ever written on the client's output", rest == b"", repr(rest))
finally:
    if client.poll() is None:
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
    sys.exit("FAIL REQ19/FWD-6: %s" % "; ".join(failures))
print("PASS REQ19/FWD-6: -W's connection outlived %d s out of contact, and ended as its ends did" % OUTAGE)
