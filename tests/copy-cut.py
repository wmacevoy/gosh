#!/usr/bin/env python3
"""REQ1/CP-2 and REQ1/CP-3: a copy that is cut off, and run again.

usage: copy-cut.py SERVER CLIENT [COPY]

COPY is the name the client answers to as the copy command: "mocp" for the
fork, which is the default; "gocp" for gosh (REQ6/NAME-1).

Copies a file to a "server" that is this machine (mocp --local, so no ssh),
kills the copy part way, and looks at what is there:

  REQ1/CP-3  no file under its own name, only a part;
  REQ1/CP-2  run again, the copy carries on from the part and does not start
             over: the part is never seen smaller than it was when cut;
             and when the source was changed in between, the part is not
             trusted: what arrives is the changed file.

The fork tested this only from inside (file-copy, which calls the copying
functions directly). This is the test from outside that the copy part must pass
before and after it is reshaped (queue item 4).

Written for gosh on 2026-11-14, and seen red that day: with the part removed
before the second run, as if nothing had been kept, "it carried on from what had
arrived" fails. A copy killed outright leaves its server
running (a stream's session waits a week for its client), so on its way out
this test stops the servers that appeared while it ran: by process id, and only
ones running SERVER's own file that carry this run's mark.

Copyright 2026 Warren D. MacEvoy. Part of gosh, under the GNU General Public
License, version 3 or later (COPYING).
"""
import os
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

SIZE = 40 * 1000 * 1000   # large enough that a copy on loopback takes several seconds
CUT_AT = 4 * 1000 * 1000  # kill the copy once this much has arrived
PATIENCE = 120            # how long anything may take before the test gives up waiting

if len(sys.argv) not in (3, 4):
    sys.exit(__doc__)
copy_name = sys.argv[3] if len(sys.argv) > 3 else "mocp"
server_path, client_path = os.path.realpath(sys.argv[1]), os.path.realpath(sys.argv[2])
# This run's mark, in the environment of everything it starts, so that what it stops on its way
# out is its own: another run may be using the same programs at the same time.
MARK = "GOSH_TEST_RUN=" + os.environ.get("GOSH_TEST_RUN", "test.%d" % os.getpid())
env = dict(os.environ, LC_ALL="C.UTF-8", MOSH_STUN="none", GOSH_TEST_RUN=MARK.split("=", 1)[1])
failures = []


def check(name, ok, more=""):
    print("%-62s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else more), flush=True)
    if not ok:
        failures.append(name)


def our_servers():
    """Process ids now running SERVER's own file that this run started: its mark is in their environment."""
    found = set()
    for pid in os.listdir("/proc"):
        if pid.isdigit():
            try:
                if os.path.realpath(os.readlink("/proc/%s/exe" % pid)) != server_path:
                    continue
                with open("/proc/%s/environ" % pid, "rb") as f:
                    if MARK.encode() in f.read().split(b"\0"):
                        found.add(int(pid))
            except OSError:
                pass
    return found


def mocp(source, there):
    return subprocess.Popen([client_path, copy_name, "-q", "--local", "--server=" + server_path,
                             source, "127.0.0.1:" + there + "/"],
                            env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return None


def cut(source, there):
    """Starts a copy and kills it once CUT_AT bytes have arrived. Returns the part's size, or None."""
    part = os.path.join(there, ".big.bin.mocp-part")
    copy = mocp(source, there)
    deadline = time.monotonic() + PATIENCE  # a limit on waiting, not something asserted
    while time.monotonic() < deadline and (size(part) or 0) < CUT_AT and copy.poll() is None:
        time.sleep(0.05)
    finished_first = copy.poll() is not None
    copy.kill()
    copy.wait()
    return None if finished_first else size(part)


def again(source, there):
    """Runs the copy to its end. Returns (exit status, the smallest the part was seen to be)."""
    part = os.path.join(there, ".big.bin.mocp-part")
    smallest = [size(part)]
    copy = mocp(source, there)

    def watch():
        while copy.poll() is None:
            now = size(part)
            if now is not None and (smallest[0] is None or now < smallest[0]):
                smallest[0] = now
            time.sleep(0.01)

    watcher = threading.Thread(target=watch, daemon=True)
    watcher.start()
    try:
        status = copy.wait(PATIENCE)
    except subprocess.TimeoutExpired:
        copy.kill()
        status = None
    watcher.join(5)
    return status, smallest[0]


def same(a, b):
    with open(a, "rb") as x, open(b, "rb") as y:
        return x.read() == y.read()


before = our_servers()
work = tempfile.mkdtemp(prefix="gosh-copy-cut-")
try:
    source = os.path.join(work, "big.bin")
    with open(source, "wb") as f:
        f.write(random.Random(7).randbytes(SIZE))  # seeded: the same bytes every run

    # Cut off, then run again with the source as it was.
    there = os.path.join(work, "there")
    os.mkdir(there)
    arrived = cut(source, there)
    check("a copy is cut off part way", arrived is not None and CUT_AT <= arrived < SIZE, repr(arrived))
    check("REQ1/CP-3: cut off, there is no file under its own name",
          not os.path.exists(os.path.join(there, "big.bin")), repr(os.listdir(there)))
    status, smallest = again(source, there)
    check("run again, the copy finishes, and the file is whole",
          status == 0 and os.path.exists(os.path.join(there, "big.bin")) and same(source, os.path.join(there, "big.bin")),
          "status %r, %r" % (status, os.listdir(there)))
    check("REQ1/CP-2: it carried on from what had arrived",
          arrived is not None and smallest is not None and smallest >= arrived,
          "cut at %r, the part was later seen at %r" % (arrived, smallest))
    check("and the part is gone", os.listdir(there) == ["big.bin"], repr(os.listdir(there)))

    # Cut off, then the source changes: what had arrived is not the start of this file.
    other = os.path.join(work, "other")
    os.mkdir(other)
    arrived = cut(source, other)
    with open(source, "r+b") as f:
        first = f.read(1)
        f.seek(0)
        f.write(bytes([first[0] ^ 0xFF]))
    status, smallest = again(source, other)
    check("REQ1/CP-2: a part of another file is not carried on from",
          status == 0 and os.path.exists(os.path.join(other, "big.bin")) and same(source, os.path.join(other, "big.bin")),
          "status %r, %r" % (status, os.listdir(other)))
finally:
    for pid in our_servers() - before:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    shutil.rmtree(work, ignore_errors=True)

if failures:
    sys.exit("FAIL REQ1/CP-2, REQ1/CP-3: %s" % "; ".join(failures))
print("PASS REQ1/CP-2, REQ1/CP-3: a copy cut off left only a part, and carried on from it")
