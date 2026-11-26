#!/usr/bin/env python3
"""REQ20/TERM-1: what a program on the server puts on the clipboard reaches the user's terminal.

usage: clipboard-session.py CLIENT SERVER

Starts SERVER on loopback running a command that, once a typed line has shown the client is up,
puts text on the clipboard (OSC 52) four times, a second apart: "AAA" with the selection c, "AAA"
with c again, "BBB" with no selection, and "CCC" with the selection p. Passes if the client hands
all four to its terminal as the program wrote them: the same text twice is two, not one (so a
copy made on the laptop between them is replaced, as it would be without gosh), and the selection
is the program's.

It stops only the two processes it started, by process id (CONTRIBUTING.md).

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
DONE = b"gosh-clipboard-done"
# base64 of AAA, BBB and CCC: what OSC 52 carries.
COMMAND = ("read line; "
           "printf '\\033]52;c;QUFB\\007'; sleep 1; "
           "printf '\\033]52;c;QUFB\\007'; sleep 1; "
           "printf '\\033]52;;QkJC\\007'; sleep 1; "
           "printf '\\033]52;p;Q0ND\\007'; sleep 1; "
           "echo gosh-clipboard-done; sleep 5")
# What the client must hand its terminal, and how many times.
WANTED = [(b"\033]52;c;QUFB\007", 2), (b"\033]52;;QkJC\007", 1), (b"\033]52;p;Q0ND\007", 1)]
WAIT_SECONDS = 20


def start_server(server, env):
    """Returns (port, key, pid) of a server we started; it detaches, so pid is from its own message."""
    done = subprocess.run(
        [server, "new", "-i", "127.0.0.1", "--", "sh", "-c", COMMAND],
        env=env, capture_output=True, text=True, timeout=WAIT_SECONDS)
    connect = re.search(r"MOSH CONNECT (\d+) (\S+)", done.stdout)
    detached = re.search(r"pid = (\d+)", done.stderr)
    if not connect or not detached:
        sys.exit("FAIL REQ20/TERM-1: %s did not start a session:\n%s%s" % (server, done.stdout, done.stderr))
    return connect.group(1), connect.group(2), int(detached.group(1))


def stop(pid):
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError:
        pass  # already gone


def drawn(client, port, key, env):
    """Runs the client on a new terminal, types a line, and returns all it wrote until the command was done."""
    pid, terminal = pty.fork()
    if pid == 0:
        os.execve(client, [client, "127.0.0.1", port], dict(env, MOSH_KEY=key))
    try:
        fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        seen = b""
        typed = False
        deadline = time.monotonic() + WAIT_SECONDS  # a limit on waiting, not something asserted
        while time.monotonic() < deadline and DONE not in seen:
            ready, _, _ = select.select([terminal], [], [], 0.5)
            if ready:
                try:
                    seen += os.read(terminal, 65536)
                except OSError:
                    break  # the client has exited
            if seen and not typed:  # it has drawn something, so it is reading keys
                os.write(terminal, TYPED)
                typed = True
        return seen
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
            sys.exit("FAIL REQ20/TERM-1: no program at %s" % program)
    env = dict(os.environ, LANG="C.UTF-8", LC_ALL="C.UTF-8", TERM="xterm-256color")
    env.pop("MOSH_KEY", None)
    port, key, server_pid = start_server(server, env)
    try:
        seen = drawn(client, port, key, env)
    finally:
        stop(server_pid)
    if DONE not in seen:
        sys.exit("FAIL REQ20/TERM-1: the session did not run its command; drawn (last 400 bytes): %r" % seen[-400:])
    wrong = ["%r %d times, not %d" % (sequence, seen.count(sequence), times)
             for sequence, times in WANTED if seen.count(sequence) != times]
    if wrong:
        sys.exit("FAIL REQ20/TERM-1: the clipboard as handed to the terminal: %s\n  every OSC 52 drawn: %r"
                 % ("; ".join(wrong), re.findall(rb"\033\]52;[^\007]*\007", seen)))
    print("PASS REQ20/TERM-1: every time the program put text on the clipboard, with its selection")


if __name__ == "__main__":
    main()
