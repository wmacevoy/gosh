#!/usr/bin/env python3
"""REQ1/CMD-1, REQ6/NAME-1, REQ1/FWD-1, REQ1/CP-7: the gosh command, from its command line to a byte at the target.

usage: gosh-command.py GOSH SERVER

GOSH is gosh-client under the name gosh (a link to it); SERVER is gosh-server.
ssh is stood in for by fake-ssh.py, which "logs in" to this machine, so nothing
here needs an ssh server or asks anything of the network.

  a terminal session   "gosh host -- command": the command logs in, starts the
                       server, becomes the client, and the command's output is
                       drawn. Once for each way the command learns the server's
                       address (proxy, remote, local).
  a forward            "gosh -N -L PORT:HOST:PORT host": no terminal; it says
                       when the forward is up, and a byte sent to the local port
                       comes back from the target.
  a stream             "gosh --stdio host -- cat" (REQ1/CP-7): two megabytes of
                       every byte value go to the command and come back the
                       same; and it finishes even when whoever started it had
                       child signals ignored, which the fork does not.

This is the thin slice of queue item 5 seen from outside: every layer between
the command line and the target is in it.

Written for gosh on 2026-11-14, and seen red that day: against upstream's
mosh-client under the name gosh, which is not the command, every check fails
but the last (it writes nothing on standard output either).
On its way out it stops the servers that appeared while it ran: by process id,
and only ones running SERVER's own file that carry this run's mark.

Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""
import fcntl
import os
import pty
import random
import select
import signal
import socket
import struct
import subprocess
import sys
import termios
import threading
import time

PATIENCE = 30  # how long anything may take before the test gives up waiting

if len(sys.argv) != 3:
    sys.exit(__doc__)
gosh, server_path = os.path.abspath(sys.argv[1]), os.path.realpath(sys.argv[2])
here = os.path.dirname(os.path.abspath(__file__))
ssh = "python3 " + os.path.join(here, "fake-ssh.py")
# This run's mark, in the environment of everything it starts, so that what it stops on its way
# out is its own: another run may be using the same programs at the same time.
MARK = "GOSH_TEST_RUN=" + os.environ.get("GOSH_TEST_RUN", "test.%d" % os.getpid())
env = dict(os.environ, LC_ALL="C.UTF-8", TERM="xterm", MOSH_STUN="none", GOSH_TEST_RUN=MARK.split("=", 1)[1])
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


def session_draws(how):
    """Runs "gosh host -- printf" on a terminal of its own; says whether the server's output was drawn."""
    # The words "he%s" and "llo" are on the command line; "hello" is only ever what the server's printf wrote.
    words = [gosh, "--ssh=" + ssh, "--server=" + server_path, "--experimental-remote-ip=" + how,
             "localhost", "--", "printf", "he%s\\n", "llo"]
    pid, terminal = pty.fork()
    if pid == 0:
        os.execve(gosh, words, env)
    seen = b""
    try:
        fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        deadline = time.monotonic() + PATIENCE  # a limit on waiting, not something asserted
        while time.monotonic() < deadline and b"hello" not in seen:
            ready, _, _ = select.select([terminal], [], [], 0.5)
            if ready:
                try:
                    data = os.read(terminal, 65536)
                except OSError:
                    break  # it has exited
                if not data:
                    break
                seen += data
    finally:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
        os.waitpid(pid, 0)
        os.close(terminal)
    return b"hello" in seen, seen[-200:]


def forward_carries_a_byte():
    """Runs "gosh -N -L" and sends bytes through the forward to a target that sends them back."""
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
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    command = subprocess.Popen(
        [gosh, "--ssh=" + ssh, "--server=" + server_path, "--experimental-remote-ip=local",
         "-N", "-L", "%d:127.0.0.1:%d" % (port, target.getsockname()[1]), "localhost"],
        env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    said = b""
    try:
        up = False
        deadline = time.monotonic() + PATIENCE
        os.set_blocking(command.stderr.fileno(), False)
        while time.monotonic() < deadline and not up and command.poll() is None:
            ready, _, _ = select.select([command.stderr], [], [], 0.5)
            if ready:
                said += command.stderr.read() or b""
                up = b"connected; forwarding" in said
        check("gosh -N -L: it says when the forward is up", up, repr(said[-200:]))
        if not up:
            return
        conn = socket.create_connection(("127.0.0.1", port), timeout=PATIENCE)
        conn.settimeout(PATIENCE)
        conn.sendall(b"a byte, and some more")
        back = b""
        try:
            while len(back) < 21:
                data = conn.recv(64)
                if not data:
                    break
                back += data
        except OSError:
            pass
        check("gosh -N -L: bytes sent to the local port come back from the target",
              back == b"a byte, and some more", repr(back))
        conn.close()
    finally:
        command.terminate()
        try:
            out, _ = command.communicate(timeout=PATIENCE)
        except subprocess.TimeoutExpired:
            command.kill()
            out, _ = command.communicate()
        check("gosh -N: nothing was written on standard output", out == b"", repr(out[-200:]))


def stdio(command, data, ignore_children=False):
    """Runs "gosh --stdio localhost -- command" with data on its standard input. Returns (status, output), or None."""
    def ignore():
        signal.signal(signal.SIGCHLD, signal.SIG_IGN)
    try:
        done = subprocess.run([gosh, "--ssh=" + ssh, "--server=" + server_path, "--experimental-remote-ip=local",
                               "--stdio", "localhost", "--"] + command,
                              env=env, input=data, capture_output=True, timeout=PATIENCE * 4,
                              preexec_fn=ignore if ignore_children else None)
    except subprocess.TimeoutExpired:
        return None
    return done.returncode, done.stdout


before = our_servers()
try:
    data = random.Random(11).randbytes(2 * 1000 * 1000)  # seeded: the same bytes every run
    got = stdio(["cat"], data)
    check("gosh --stdio: two megabytes of every byte value go and come back the same",
          got is not None and got[0] == 0 and got[1] == data,
          "timed out" if got is None else "status %d, %d bytes" % (got[0], len(got[1])))
    got = stdio(["echo", "hello"], b"", ignore_children=True)
    check("gosh --stdio: it finishes when started with child signals ignored",
          got is not None and got[1] == b"hello\n", repr(got))

    for how in ("proxy", "remote", "local"):
        drawn, tail = session_draws(how)
        check('gosh host -- command: a terminal session, the address by "%s"' % how, drawn, repr(tail))
    forward_carries_a_byte()
finally:
    for pid in our_servers() - before:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass

if failures:
    sys.exit("FAIL REQ1/CMD-1, REQ6/NAME-1, REQ1/FWD-1, REQ1/CP-7: %s" % "; ".join(failures))
print("PASS REQ1/CMD-1, REQ6/NAME-1, REQ1/FWD-1, REQ1/CP-7: the gosh command, a session, a forward and a stream")
