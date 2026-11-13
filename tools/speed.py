#!/usr/bin/env python3
"""How fast a forward carries bytes, end to end, on this machine.

REQ6/SPEED-1: the figure a part of gosh is held to. A forward in gosh carries
bytes at not more than 10% lower than mosh-1.4.0-pf.7 does on the same machine,
session by UDP and session by the relay. It measures the whole, not a layer:
bytes into a TCP forward at the client, out of it at the server.

usage: tools/speed.py SERVER CLIENT [MEGABYTES [RUNS]]
  SERVER, CLIENT  the two programs to measure (pf.7's, or gosh's)
  MEGABYTES       how much to send each run (20)
  RUNS            how many runs of each way; the median is the figure (5)

Two figures for each way (design v2, question 7):
  Mbit/s      how fast the bytes arrived
  CPU s/MB    processor seconds the programs spent per megabyte carried, summed
              over every process running either program's file while the bytes
              moved. By UDP on loopback the first figure is unsteady, and this
              one says what the programs cost whatever the clock did.

From the fork's workspace (../mosh-dev/tools/speed.py). Changed here: the two
programs are arguments, and the second figure is added.

Two ways:
  udp     the session by UDP, as mosh has it
  relay   the session carried by the relays' stream, as through ssh
          (the remote relay run directly, where ssh would run it)

Everything is on loopback, with no delay and no loss, so what it shows is
what the programs cost and not what a network does.  It asks nothing of any
server on the Internet, and stops what it started by process id.
"""

import os
import re
import socket
import statistics
import subprocess
import sys
import threading
import time

if len(sys.argv) < 3:
    sys.exit(__doc__)
server_path, client_path = os.path.realpath(sys.argv[1]), os.path.realpath(sys.argv[2])
megabytes = int(sys.argv[3]) if len(sys.argv) > 3 else 20
runs = int(sys.argv[4]) if len(sys.argv) > 4 else 5
total = megabytes * 1000 * 1000
TICKS = os.sysconf("SC_CLK_TCK")


def cpu_seconds():
    """Processor time used so far by every live process running either program's file (Linux: /proc)."""
    ticks = 0
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            if os.path.realpath(os.readlink("/proc/%s/exe" % pid)) not in (server_path, client_path):
                continue
            with open("/proc/%s/stat" % pid) as f:
                fields = f.read().rsplit(")", 1)[1].split()  # after the name, which may hold spaces
            ticks += int(fields[11]) + int(fields[12])  # utime, stime
        except (OSError, IndexError, ValueError):
            pass  # it ended while we looked
    return ticks / TICKS


def one_run(way):
    """(megabits a second, processor seconds per megabyte) for one session carrying `total` bytes, or None."""
    sink = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sink.bind(("127.0.0.1", 0))
    sink.listen(1)
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    spec = "%d:127.0.0.1:%d" % (port, sink.getsockname()[1])
    env = dict(os.environ, LC_ALL="C.UTF-8", MOSH_STUN="none", MOSH_PUNCH="never")

    started = subprocess.run([server_path, "new", "-i", "127.0.0.1", "-L", spec, "--", "/bin/sh", "-c", "while :; do sleep 1000; done"],
                             env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
    match = re.search(r"MOSH CONNECT (\d+) (\S+)", started.stdout)
    server_pid = re.search(r"pid = (\d+)", started.stderr)
    if not match:
        return None
    client_env = dict(env, MOSH_KEY=match.group(2))
    if way == "relay":
        client_env["MOSH_RELAY"] = "always"
        client_env["MOSH_RELAY_COMMAND"] = "\x1f".join([server_path, "relay", "127.0.0.1", match.group(1)])
    client = subprocess.Popen([client_path, "-N", "-L", spec, "127.0.0.1", match.group(1)],
                              env=client_env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for line in client.stderr:
            if b"connected; forwarding" in line:
                break
        else:
            return None

        arrived = [0, None]

        def receive():
            conn, _ = sink.accept()
            while arrived[0] < total:
                data = conn.recv(1 << 16)
                if not data:
                    break
                arrived[0] += len(data)
            arrived[1] = time.monotonic()
            conn.close()

        receiver = threading.Thread(target=receive, daemon=True)
        receiver.start()
        conn = socket.create_connection(("127.0.0.1", port), timeout=30)
        block = bytes(1 << 16)
        cpu_before = cpu_seconds()
        began = time.monotonic()
        left = total
        while left > 0:
            conn.sendall(block[:left])
            left -= min(left, len(block))
        receiver.join(300)
        cpu_used = cpu_seconds() - cpu_before
        conn.close()
        if arrived[0] < total or arrived[1] is None:
            return None
        return total * 8 / 1e6 / (arrived[1] - began), cpu_used / megabytes
    finally:
        client.terminate()
        try:
            client.wait(20)
        except subprocess.TimeoutExpired:
            client.kill()
        if server_pid:
            try:
                os.kill(int(server_pid.group(1)), 15)
            except OSError:
                pass
        sink.close()


version = subprocess.run([client_path, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
print("speed: %s; %d MB a run, %d runs each way, loopback" % (version, megabytes, runs))
failed = False
for way in ("udp", "relay"):
    figures = [one_run(way) for _ in range(runs)]
    if None in figures:
        print("speed: %-5s FAIL: %d of %d runs did not finish" % (way, figures.count(None), runs))
        failed = True
        continue
    rates = [f[0] for f in figures]
    costs = [f[1] for f in figures]
    print("speed: %-5s median %.1f Mbit/s (lowest %.1f, highest %.1f); median %.4f CPU s/MB (lowest %.4f, highest %.4f)"
          % (way, statistics.median(rates), min(rates), max(rates), statistics.median(costs), min(costs), max(costs)))
sys.exit(1 if failed else 0)
