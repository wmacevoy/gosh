#!/usr/bin/env python3
"""How fast a forward carries bytes, end to end, on this machine.

REQ6/SPEED-1: the figure a part of gosh is held to. A forward in gosh carries
bytes at not more than 10% lower than mosh-1.4.0-pf.7 does on the same machine,
session by UDP and session by the relay. It measures the whole, not a layer:
bytes into a TCP forward at the client, out of it at the server.

usage: tools/speed.py SERVER CLIENT [MEGABYTES [RUNS]]
       tools/speed.py --against REF_SERVER REF_CLIENT SERVER CLIENT [MEGABYTES [RUNS]]
  SERVER, CLIENT  the two programs to measure (pf.7's, or gosh's)
  MEGABYTES       how much to send each run (20)
  RUNS            how many runs of each way; the median is the figure (5; with --against,
                  30 by UDP and 15 by the relay)

With --against it is the gate: the reference (pf.7's programs) and the programs
under test are run turn and turn about, so that both see the same machine in
the same minutes, and it fails (exit 1) if the median rate of the programs
under test is more than 10% below the reference's, either way. "Overall" is the
requirement's word: bytes end to end, not any layer. A way the programs under
test cannot do at all yet is reported and not counted (the relay, until gosh
has one).

Why 30 and 15. By UDP the rate varies from run to run (30 runs of pf.7 on
2026-11-14: 13.5 to 28.9 Mbit/s, median 17.0), and the median of 15 was not
steady enough: pf.7 against itself came out at 90%, on the line. Resampling
those 30 runs, two medians of 15 differ by more than 10% about 3 times in 100
by chance alone, two medians of 30 about 3 times in 1,000. By the relay the
rate is steady (pf.7 against itself: 99% with 15). "speed.py --against A A",
a thing against itself, repeats that check.

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

argv = sys.argv[1:]
against = None
if argv[:1] == ["--against"]:
    if len(argv) < 5:
        sys.exit(__doc__)
    against = (os.path.realpath(argv[1]), os.path.realpath(argv[2]))
    argv = argv[3:]
if len(argv) < 2:
    sys.exit(__doc__)
server_path, client_path = os.path.realpath(argv[0]), os.path.realpath(argv[1])
megabytes = int(argv[2]) if len(argv) > 2 else 20
runs = int(argv[3]) if len(argv) > 3 else (None if against else 5)  # None: the gate's own number for each way
GATE_RUNS = {"udp": 30, "relay": 15}
total = megabytes * 1000 * 1000
TICKS = os.sysconf("SC_CLK_TCK")


def cpu_seconds(server_path, client_path):
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


def one_run(way, server_path, client_path):
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
        by_relay = False
        for line in client.stderr:
            # (a client told to use the relay says so; one that knows nothing of relays says nothing
            # and goes by UDP, which must not be measured as if it were the relay)
            by_relay = by_relay or b"through ssh" in line
            if b"connected; forwarding" in line:
                break
        else:
            return None
        if way == "relay" and not by_relay:
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
        cpu_before = cpu_seconds(server_path, client_path)
        began = time.monotonic()
        left = total
        while left > 0:
            conn.sendall(block[:left])
            left -= min(left, len(block))
        receiver.join(300)
        cpu_used = cpu_seconds(server_path, client_path) - cpu_before
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


def version(client):
    return subprocess.run([client, "--version"], capture_output=True, text=True).stdout.splitlines()[0]


def said(way, figures):
    rates = [f[0] for f in figures]
    costs = [f[1] for f in figures]
    return ("%-5s median %.1f Mbit/s (lowest %.1f, highest %.1f); median %.4f CPU s/MB (lowest %.4f, highest %.4f)"
            % (way, statistics.median(rates), min(rates), max(rates), statistics.median(costs), min(costs), max(costs)))


failed = False
if not against:
    print("speed: %s; %d MB a run, %d runs each way, loopback" % (version(client_path), megabytes, runs))
    for way in ("udp", "relay"):
        figures = [one_run(way, server_path, client_path) for _ in range(runs)]
        if None in figures:
            print("speed: %-5s FAIL: %d of %d runs did not finish" % (way, figures.count(None), runs))
            failed = True
            continue
        print("speed: " + said(way, figures))
    sys.exit(1 if failed else 0)

# The gate (REQ6/SPEED-1): the reference and the programs under test, turn and turn about.
print("speed: reference %s" % version(against[1]))
print("speed: under test %s; %d MB a run, alternating, loopback" % (version(client_path), megabytes))
for way in ("udp", "relay"):
    count = runs if runs is not None else GATE_RUNS[way]
    if one_run(way, server_path, client_path) is None:  # (a first run, not counted, that also warms both up)
        print("speed: %-5s NOT COUNTED: the programs under test cannot do this way yet" % way)
        continue
    reference, tested = [], []
    for _ in range(count):
        reference.append(one_run(way, against[0], against[1]))
        tested.append(one_run(way, server_path, client_path))
    if None in reference or None in tested:
        print("speed: %-5s FAIL: %d reference and %d tested runs did not finish" % (way, reference.count(None), tested.count(None)))
        failed = True
        continue
    print("speed: reference  " + said(way, reference))
    print("speed: under test " + said(way, tested))
    ratio = statistics.median(f[0] for f in tested) / statistics.median(f[0] for f in reference)
    ok = ratio >= 0.9
    print("speed: %-5s %s REQ6/SPEED-1: over %d runs of each, the rate under test is %.0f%% of the reference's (not less than 90%% passes)"
          % (way, "PASS" if ok else "FAIL", count, ratio * 100))
    failed = failed or not ok
sys.exit(1 if failed else 0)
