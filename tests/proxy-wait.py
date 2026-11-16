#!/usr/bin/env python3
"""REQ17/PATH-8: an address that does not answer gives way to the next within seconds.

usage: proxy-wait.py GOSH

GOSH is gosh-client under the name gosh. ssh reaches the host through gosh's proxy command
(`gosh --mosh-fake-proxy FAMILY HOST PORT`), which tries the host's addresses in the order the
family asks for. Here localhost's IPv6 address, ::1, is made to swallow connections: a listener
whose queue is full, so the system drops each new connection's first packet, as a network that
loses everything does. Then:

  gives way    with IPv6 asked for first, the proxy leaves ::1 within a few seconds and reaches
               127.0.0.1; until 2026-11-16 it waited as long as the system does (about two
               minutes on Linux).
  the last     an address with none after it is waited for as long as the system waits: a host
               on a slow network is not given up on early. Seen by the proxy still waiting at
               twice the time limit.

Linux only (where a full queue drops a connection's first packet); it says SKIPPED elsewhere, and
where localhost does not have both ::1 and 127.0.0.1. Nothing leaves this machine.

Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""
import os
import socket
import subprocess
import sys
import time

LIMIT = 3  # the proxy's time limit for an address with another after it (proxy.cc)

if len(sys.argv) != 2:
    sys.exit(__doc__)
gosh = os.path.abspath(sys.argv[1])
found = {a[4][0] for a in socket.getaddrinfo("localhost", 1, 0, socket.SOCK_STREAM)}
if not sys.platform.startswith("linux") or not {"::1", "127.0.0.1"} <= found:
    print("SKIPPED REQ17/PATH-8: needs Linux and a localhost with both ::1 and 127.0.0.1")
    sys.exit(0)

failures = []


def check(what, ok, got=""):
    print("%-70s %s" % (what, "ok" if ok else "FAIL " + str(got)))
    if not ok:
        failures.append(what)


def swallowing(family, address, port=0):
    """A listener at address whose queue is full: a new connection's first packet is dropped."""
    s = socket.socket(family, socket.SOCK_STREAM)
    if family == socket.AF_INET6:
        s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
    s.bind((address, port))
    s.listen(0)
    fillers = []
    for _ in range(3):  # (a queue of 0 still holds one or two, depending on the kernel)
        c = socket.socket(family, socket.SOCK_STREAM)
        c.setblocking(False)
        c.connect_ex(s.getsockname()[:2])
        fillers.append(c)
    time.sleep(0.2)
    return s, fillers


def proxy(family, port):
    return subprocess.Popen([gosh, "--mosh-fake-proxy", family, "localhost", str(port)],
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


# gives way: ::1 swallows, 127.0.0.1 on the same port answers.
hole, fillers = swallowing(socket.AF_INET6, "::1")
port = hole.getsockname()[1]
answer = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
answer.bind(("127.0.0.1", port))
answer.listen(5)
answer.settimeout(LIMIT * 4)
started = time.monotonic()
p = proxy("prefer-inet6", port)
try:
    conn, _ = answer.accept()
    took = time.monotonic() - started
    conn.close()
    check("gives way: ::1 not answering, 127.0.0.1 is reached", True)
    check("gives way: within the time limit and a little more (%.1f s)" % took, took < LIMIT + 2, "%.1f s" % took)
except socket.timeout:
    check("gives way: ::1 not answering, 127.0.0.1 is reached", False, "nothing in %d s" % (LIMIT * 4))
p.kill()
p.wait()
for s in [hole, answer] + fillers:
    s.close()

# the last: 127.0.0.1 alone, swallowing; the proxy is still trying at twice the limit.
hole, fillers = swallowing(socket.AF_INET, "127.0.0.1")
p = proxy("inet", hole.getsockname()[1])
time.sleep(LIMIT * 2)
check("the last: an address with none after it is still being waited for", p.poll() is None,
      "gave up: %r" % (p.stderr.read() if p.poll() is not None else b""))
p.kill()
p.wait()
for s in [hole] + fillers:
    s.close()

if failures:
    print("FAIL REQ17/PATH-8: " + "; ".join(failures))
    sys.exit(1)
print("PASS REQ17/PATH-8: an address that does not answer gives way to the next within seconds; the last is waited for")
