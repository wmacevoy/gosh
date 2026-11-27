#!/usr/bin/env python3
"""REQ21/SHAPE-1: either end may be a stock mosh and the terminal still works.

usage: stock-session.py CLIENT SERVER

Starts SERVER (a mosh-server) on loopback running a small command, joins it
with CLIENT (a mosh-client) on a terminal of its own, types a line, and passes
if the command's answer to that line appears on the client's screen. That is
keys going up and the screen coming down: a terminal session.

The check runs it twice: gosh's client with a stock server, and a stock client
with gosh's server.

It stops only the two processes it started, by process id: the machine may be
running other mosh sessions that are not ours to stop (CONTRIBUTING.md).

Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time

TYPED = b"hello\n"
# What the server's command prints once it has read the typed line. The client
# cannot show this by itself (it only echoes keys), so seeing it proves the
# line went to the server and the screen came back.
ANSWER = b"gosh-heard:hello"
COMMAND = 'read line; echo "gosh-heard:$line"; sleep 5'
WAIT_SECONDS = 20


def start_server(server, env):
    """Returns (port, key, pid) of a server we started; it detaches, so pid is from its own message."""
    done = subprocess.run(
        [server, "new", "-i", "127.0.0.1", "--", "sh", "-c", COMMAND],
        env=env, capture_output=True, text=True, timeout=WAIT_SECONDS)
    connect = re.search(r"MOSH CONNECT (\d+) (\S+)", done.stdout)
    detached = re.search(r"pid = (\d+)", done.stderr)
    if not connect or not detached:
        sys.exit("FAIL REQ21/SHAPE-1: %s did not start a session:\n%s%s" % (server, done.stdout, done.stderr))
    return connect.group(1), connect.group(2), int(detached.group(1))


def stop(pid):
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError:
        pass  # already gone


def screen_shows_answer(client, port, key, env):
    """Runs the client on a new terminal, types a line: whether the answer was drawn, what was, and whether it typed."""
    pid, terminal = pty.fork()
    if pid == 0:
        os.execve(client, [client, "127.0.0.1", port], dict(env, MOSH_KEY=key))
    try:
        fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        seen = b""
        typed = False
        deadline = time.monotonic() + WAIT_SECONDS  # a limit on waiting, not something asserted
        while time.monotonic() < deadline and ANSWER not in seen:
            ready, _, _ = select.select([terminal], [], [], 0.5)
            if ready:
                try:
                    seen += os.read(terminal, 65536)
                except OSError:
                    break  # the client has exited
            if seen and not typed:  # it has drawn something, so it is reading keys
                os.write(terminal, TYPED)
                typed = True
        return ANSWER in seen, seen, typed
    finally:
        stop(pid)
        os.waitpid(pid, 0)
        os.close(terminal)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    client, server = sys.argv[1], sys.argv[2]
    for program in (client, server):
        if not os.access(program, os.X_OK):
            sys.exit("FAIL REQ21/SHAPE-1: no program at %s" % program)
    # mosh insists on a UTF-8 locale at both ends.
    env = dict(os.environ, LANG="C.UTF-8", LC_ALL="C.UTF-8", TERM="xterm-256color")
    env.pop("MOSH_KEY", None)
    port, key, server_pid = start_server(server, env)
    try:
        ok, seen, typed = screen_shows_answer(client, port, key, env)
    finally:
        stop(server_pid)
    what = "%s with %s" % (client, server)
    if not ok:
        # (what the client drew, and whether it was typed to: so that a failure says whether the
        # session never came up or the line was lost; seen once at random in the check, 2026-11-16)
        sys.exit("FAIL REQ21/SHAPE-1: no terminal session: %s\n  typed: %s; drawn (last 400 bytes): %r" % (what, typed, seen[-400:]))
    print("PASS REQ21/SHAPE-1: a terminal session: %s" % what)


if __name__ == "__main__":
    main()
