#!/usr/bin/env python3
"""REQ8/CMD-3: which server gosh asks a host for, and what it says of a stock one.

usage: gosh-stock.py GOSH GOSH_SERVER STOCK_SERVER

GOSH is gosh-client under the name gosh; GOSH_SERVER is gosh-server;
STOCK_SERVER is a stock mosh-server (mosh 1.4.0). ssh is stood in for by
fake-ssh.py, and the "host" is this machine with a home directory and a PATH
made for each case, so that the host has exactly the servers the case says:

  a host with gosh-server    gosh asks for it; a forward works; nothing is
                             said about a stock server.
  a host with only           gosh says once that the host has no gosh-server
  mosh-server                and names the forward that will not be done; it
                             asks the server for none (a stock server given -L
                             would complain); the session still comes up; and
                             a terminal session works regardless.
  a host with neither        gosh says it found no server, and stops: status 1,
                             not a hang.

  a stock server named       with --server=PATH the server is taken to be gosh's
  by the user                and asked for everything; when it turns out not to
                             forward, gosh says so and does not wait in silence.

Written for gosh on 2026-11-14, and seen red that day: with the fork's command
unchanged (it asks only for mosh-server and says nothing), the first and second
cases fail. It stops nothing but what it started itself.

Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""
import fcntl
import os
import pty
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time
from variables import both

PATIENCE = 30  # how long anything may take before the test gives up waiting

if len(sys.argv) != 4:
    sys.exit(__doc__)
gosh, gosh_server, stock_server = (os.path.abspath(p) for p in sys.argv[1:4])
here = os.path.dirname(os.path.abspath(__file__))
failures = []


def check(name, ok, more=""):
    print("%-66s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else more), flush=True)
    if not ok:
        failures.append(name)


work = tempfile.mkdtemp(prefix="gosh-stock-")
# The host's PATH: only what a login needs, so that no server is found that a case did not put there.
tools = os.path.join(work, "tools")
os.mkdir(tools)
# (sleep: a session with no command runs a loop of sleeps; without it that loop spins)
for name, path in (("python3", sys.executable), ("sh", "/bin/sh"), ("printf", shutil.which("printf")),
                   ("sleep", shutil.which("sleep"))):
    os.symlink(path, os.path.join(tools, name))
ssh = "python3 " + os.path.join(here, "fake-ssh.py")


def host(servers):
    """The environment of a host that has these servers, by name, in its ~/.local/bin."""
    home = tempfile.mkdtemp(prefix="home-", dir=work)
    os.makedirs(os.path.join(home, ".local", "bin"))
    for name, path in servers.items():
        os.symlink(path, os.path.join(home, ".local", "bin", name))
    return both(dict(os.environ, HOME=home, PATH=tools, LC_ALL="C.UTF-8", TERM="xterm", MOSH_STUN="none"))


def echo_target():
    target = socket.socket()
    target.bind(("127.0.0.1", 0))
    target.listen(1)

    def echo():
        try:
            conn, _ = target.accept()
            while True:
                data = conn.recv(65536)
                if not data:
                    break
                conn.sendall(data)
        except OSError:
            pass

    threading.Thread(target=echo, daemon=True).start()
    return target


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def headless(env, spec, until, more=()):
    """Runs "gosh -N -L spec localhost" until it has said `until` on standard error (or gives up).
    more: words to put before them. Returns (the process, what it said)."""
    process = subprocess.Popen([gosh, "--ssh=" + ssh, "--experimental-remote-ip=local"] + list(more)
                               + ["-N", "-L", spec, "localhost"],
                               env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    said = b""
    os.set_blocking(process.stderr.fileno(), False)
    deadline = time.monotonic() + PATIENCE  # a limit on waiting, not something asserted
    while time.monotonic() < deadline and until not in said and process.poll() is None:
        ready, _, _ = select.select([process.stderr], [], [], 0.5)
        if ready:
            said += process.stderr.read() or b""
    return process, said


def stop(process):
    """Ends it, and returns what it wrote on standard output while it ran."""
    process.terminate()
    try:
        out, _ = process.communicate(timeout=PATIENCE)
    except subprocess.TimeoutExpired:
        process.kill()
        out, _ = process.communicate()
    return out


def through(port, data):
    """What comes back when data is sent to the local port, or None if nothing listens there."""
    try:
        conn = socket.create_connection(("127.0.0.1", port), timeout=PATIENCE)
    except OSError:
        return None
    conn.settimeout(PATIENCE)
    back = b""
    try:
        conn.sendall(data)
        while len(back) < len(data):
            got = conn.recv(64)
            if not got:
                break
            back += got
    except OSError:
        pass
    conn.close()
    return back


def session(env):
    """Runs "gosh localhost -- printf" on a terminal of its own; returns everything it wrote."""
    words = [gosh, "--ssh=" + ssh, "--experimental-remote-ip=local", "localhost", "--", "printf", "he%s\\n", "llo"]
    pid, terminal = pty.fork()
    if pid == 0:
        os.execve(gosh, words, env)
    seen = b""
    try:
        fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        deadline = time.monotonic() + PATIENCE
        while time.monotonic() < deadline and b"hello" not in seen:
            ready, _, _ = select.select([terminal], [], [], 0.5)
            if ready:
                try:
                    data = os.read(terminal, 65536)
                except OSError:
                    break
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
    return seen


NOTICE = b"has no gosh-server"
try:
    # ---- a host with gosh-server
    target, port = echo_target(), free_port()
    spec = "%d:127.0.0.1:%d" % (port, target.getsockname()[1])
    process, said = headless(host({"gosh-server": gosh_server, "mosh-server": stock_server}), spec, b"connected; forwarding")
    check("a host with gosh-server: the forward comes up", b"connected; forwarding" in said, repr(said[-300:]))
    check("a host with gosh-server: and carries bytes", through(port, b"through gosh") == b"through gosh")
    check("a host with gosh-server: nothing is said of a stock server", NOTICE not in said, repr(said[-300:]))
    check("a host with gosh-server: -N writes nothing on standard output", stop(process) == b"")

    # ---- the same host, the session over ssh from the start: the remote relay must be gosh-server's too,
    # though a stock mosh-server is also there (REQ1/PATH-1; found 2026-11-16: it ran mosh-server by name)
    target, port = echo_target(), free_port()
    spec = "%d:127.0.0.1:%d" % (port, target.getsockname()[1])
    process, said = headless(host({"gosh-server": gosh_server, "mosh-server": stock_server}), spec,
                             b"connected; forwarding", more=["--relay=always"])
    check("a host with both, over ssh: the forward comes up", b"connected; forwarding" in said, repr(said[-300:]))
    check("a host with both, over ssh: and carries bytes, through gosh-server's relay",
          through(port, b"through the relay") == b"through the relay")
    stop(process)

    # ---- a host with only a stock mosh-server
    stock_host = host({"mosh-server": stock_server})
    target, port = echo_target(), free_port()
    spec = "%d:127.0.0.1:%d" % (port, target.getsockname()[1])
    process, said = headless(stock_host, spec, b"gosh: connected.")
    wanted = ("gosh: localhost has no gosh-server, so its mosh-server is used. "
              "That one cannot do these, and is not asked to: -L %s." % spec).encode()
    check("a stock host: it says which server it got, and the forward not done", wanted in said, repr(said[-400:]))
    check("a stock host: it says so once", said.count(NOTICE) == 1, repr(said[-400:]))
    check("a stock host: the session comes up all the same", b"gosh: connected." in said, repr(said[-400:]))
    check("a stock host: nothing listens for the forward", through(port, b"x") is None)
    out = stop(process)
    # A stock server given a word it does not know says "invalid option" and prints its usage, which the
    # command passes on; so their absence from everything gosh wrote is how "asked for no forward" shows.
    check("a stock host: the server was asked for no forward (it did not complain)",
          not any(w in said + out for w in (b"Usage:", b"invalid option")), repr((said + out)[-400:]))
    check("a stock host: -N writes nothing on standard output", out == b"", repr(out[-200:]))
    done = subprocess.run([gosh, "--ssh=" + ssh, "--experimental-remote-ip=local", "--stdio", "localhost", "--", "echo", "hi"],
                          env=stock_host, stdin=subprocess.DEVNULL, capture_output=True, timeout=PATIENCE * 2)
    check("a stock host: a stream is refused in words, with status 1, and nothing is run",
          done.returncode == 1 and b"has no gosh-server, and its mosh-server cannot do this (--stdio)" in done.stderr
          and done.stdout == b"", repr((done.returncode, done.stdout[-100:], done.stderr[-200:])))
    drawn = session(stock_host)
    check("a stock host: a terminal session works regardless", b"hello" in drawn, repr(drawn[-300:]))
    check("a stock host: and there too it says so once", drawn.count(NOTICE) == 1, repr(drawn[-300:]))

    # ---- a stock server that the user names: it is asked for everything, and cannot do it
    target, port = echo_target(), free_port()
    spec = "%d:127.0.0.1:%d" % (port, target.getsockname()[1])
    process, said = headless(host({}), spec, b"does not forward", more=["--server=" + stock_server])
    check("a stock server named with --server: gosh says that it does not forward",
          b"connected, but the server does not forward" in said, repr(said[-400:]))
    check("a stock server named with --server: and does not say it is forwarding",
          b"connected; forwarding" not in said, repr(said[-400:]))
    check("a stock server named with --server: -N writes nothing on standard output", stop(process) == b"")

    # ---- a host with neither
    done = subprocess.run([gosh, "--ssh=" + ssh, "--experimental-remote-ip=local", "-N", "localhost"],
                          env=host({}), stdin=subprocess.DEVNULL, capture_output=True, timeout=PATIENCE * 2)
    check("a host with no server: gosh stops with status 1", done.returncode == 1, repr(done.returncode))
    check("a host with no server: and says that it found none",
          b"startup message" in done.stderr, repr(done.stderr[-300:]))
finally:
    shutil.rmtree(work, ignore_errors=True)

if failures:
    sys.exit("FAIL REQ8/CMD-3: %s" % "; ".join(failures))
print("PASS REQ8/CMD-3: gosh asks for gosh-server, then mosh-server, and says what a stock one will not do")
