#!/usr/bin/env python3
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""REQ1/PATH-4, REQ1/PATH-5: a meetup says where an asker appears to be; with a key, only to those who show it.

usage: meetup-answers.py SERVER

Starts "SERVER meetup PORT" twice, on loopback: without a key, and with one made
by "SERVER meetup-key". Asks each the STUN question as a stranger would.

  without a key   it answers: a Binding Response naming the asker's own address
                  and port. What is not a Binding Request gets no answer.
  with a key      a stranger, who cannot show the key, gets no answer at all.

What a holder of the key gets, and what an onlooker can do with a proof they
have seen, is tested with the real cipher and no socket in src/meetup/
(proof-test, passing-test). This test is the process: that it starts, listens,
answers and keeps quiet. It was seen red on 2026-11-15 against a server made to
answer everyone. It stops only the two meetups it started, by process id.
"""
import os
import socket
import struct
import subprocess
import sys
import time

if len(sys.argv) != 2:
    sys.exit(__doc__)
server = os.path.abspath(sys.argv[1])
failures = []


def check(name, ok, detail=""):
    print("%-76s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else detail), flush=True)
    if not ok:
        failures.append(name)


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


COOKIE = 0x2112A442
REQUEST = struct.pack(">HHI", 1, 0, COOKIE) + b"twelve bytes"


def ask(port, what, wait=1.0):
    """Sends this to the meetup; returns (the answer or None, the asker's own port)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.settimeout(wait)
    s.sendto(what, ("127.0.0.1", port))
    try:
        answer = s.recv(2048)
    except OSError:
        answer = None
    own = s.getsockname()[1]
    s.close()
    return answer, own


def started(env):
    port = free_port()
    meetup = subprocess.Popen([server, "meetup", str(port)], env=env, stdin=subprocess.DEVNULL)
    time.sleep(0.5)
    return meetup, port


env = dict(os.environ, LC_ALL="C.UTF-8")
env.pop("MOSH_MEETUP_KEY", None)
meetups = []
try:
    key = subprocess.run([server, "meetup-key"], capture_output=True, text=True, timeout=20).stdout.strip()
    check('"meetup-key" prints a key of 22 characters', len(key) == 22, repr(key))

    plain, port = started(env)
    meetups.append(plain)
    check("a meetup without a key starts and stays", plain.poll() is None)
    answer, own = ask(port, REQUEST)
    expected = (struct.pack(">HHI", 0x0101, 12, COOKIE) + b"twelve bytes" + struct.pack(">HHBB", 0x0020, 8, 0, 1)
                + struct.pack(">H", own ^ (COOKIE >> 16)) + struct.pack(">I", 0x7F000001 ^ COOKIE))
    check("asked where the asker appears to be, it says: 127.0.0.1 and the asker's own port", answer == expected,
          repr(answer))
    check("what is not that question gets no answer", ask(port, b"hello there, are you a meetup?", 0.5)[0] is None)

    keyed, port = started(dict(env, MOSH_MEETUP_KEY=key))
    meetups.append(keyed)
    check("a meetup with a key starts and stays", keyed.poll() is None)
    check("a stranger, who cannot show the key, gets no answer", ask(port, REQUEST, 0.7)[0] is None)

    bad = subprocess.run([server, "meetup", str(free_port())], env=dict(env, MOSH_MEETUP_KEY="not a key"),
                         stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=20)
    check("given what is not a key, it says so and does not start", bad.returncode == 1 and "not a key" in bad.stderr,
          repr(bad.stderr))
finally:
    for m in meetups:
        if m.poll() is None:
            m.terminate()
            try:
                m.wait(timeout=5)
            except subprocess.TimeoutExpired:
                m.kill()

if failures:
    sys.exit("FAIL REQ1/PATH-4, REQ1/PATH-5: %s" % "; ".join(failures))
print("PASS REQ1/PATH-4, REQ1/PATH-5: a meetup answers the question, and with a key says nothing to a stranger")
