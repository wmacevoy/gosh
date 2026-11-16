#!/usr/bin/env python3
"""REQ1/CP-6: a receiving end writes only inside its destination, whatever names the sending end gives it.

usage: copy-liar.py SERVER CLIENT

Starts SERVER with the receiving end of a copy as its end of a stream, and
CLIENT with, at its end, not gosh's sender but a sender of this test's own that
speaks the copy protocol and lies: it offers a file under a name that is not
inside the destination, and then, whatever it is told, goes on to send it.

  an honest name      "inside.txt": the receiver asks for the file. (This is
                      what makes the test able to fail: a receiver that asked
                      for nothing would pass the cases below for nothing.)
  names that lie      "../outside.txt", "sub/../../outside.txt" and a path
                      from the root: the receiver asks for nothing, the stream
                      ends, and no file or part of a file appears outside the
                      destination.

The fork tested this only from inside (file-copy, which calls the name check
directly, and a fuzzer). This is the test from outside that copy must pass
before and after it is reshaped (queue items 4 and 9).

It runs itself a second time as the lying sender ("--liar"), because a
stream's process is started by the client and not by this test.

Written for gosh on 2026-11-14, and seen red that day: with the receiver's name
check made to accept anything, a part of a file is written outside the
destination for each lying name. It stops only the server it started, by the
process id that server reported.

Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""
import os
import re
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
from variables import both

PATIENCE = 30  # how long anything may take before the test gives up waiting

# The copy protocol, as far as a liar needs it (src/copy/filecopy.h): each message is a type
# (one byte), a length (four, most significant first) and that many bytes.
HELLO, LIST, LIST_END, WANT, PLAN_END, FILE_BEGIN, DATA, FILE_END = 1, 2, 3, 4, 5, 6, 7, 8
MAGIC = b"mocp-1"


def message(kind, body=b""):
    return struct.pack(">BI", kind, len(body)) + body


def liar(name, report, lying):
    """The sending end, as a stream's process: standard input and output are the stream.
    lying: go on to send the file whatever the receiver says."""
    out, inp = sys.stdout.buffer, sys.stdin.buffer
    seen = []

    def say(data):
        try:
            out.write(data)
            out.flush()
        except OSError:
            pass  # the receiver has gone, which is what it should do

    def hear():
        head = inp.read(5)
        if len(head) < 5:
            return None
        kind, length = struct.unpack(">BI", head)
        inp.read(length)
        return kind

    say(message(HELLO, MAGIC))
    say(message(HELLO, b"\x01"))  # several things to copy: the destination is a directory
    seen.append(hear())           # the receiver's greeting

    name_bytes = name.encode()
    entry = (b"F" + b"\x00" + struct.pack(">I", len(name_bytes)) + name_bytes
             + struct.pack(">QQI", 5, 1700000000, 0o644))  # a file of 5 bytes
    say(message(LIST, entry))
    say(message(LIST_END, b"\x00"))
    if lying:
        # Whatever is said back, go on at once as a sender would that had been asked for the file.
        say(message(FILE_BEGIN, struct.pack(">IQ", 0, 0)))
        say(message(DATA, b"owned"))
        say(message(FILE_END))
    # (An honest offer stops here and listens. To go on with a file it has no proper end for
    # would end the receiver, and whether its answer got back first would be a matter of luck.)
    while True:
        kind = hear()
        if kind is None:
            break
        seen.append(kind)
        if kind == PLAN_END:
            break
    with open(report, "w") as f:
        f.write(" ".join(str(k) for k in seen))
    sys.exit(0 if WANT in seen else 3)


def offered(server, client, work, name, lying):
    """Offers one name to a receiver whose destination is work/dest; lying: and then sends the file
    unasked. Returns the message types the sender heard."""
    dest = os.path.join(work, "dest")
    shutil.rmtree(dest, ignore_errors=True)
    os.mkdir(dest)
    report = os.path.join(work, "report")
    if os.path.exists(report):
        os.remove(report)
    env = both(dict(os.environ, LC_ALL="C.UTF-8", MOSH_STUN="none"))

    receiver = "\x1f".join(["@copy", "receive", "-q", "--", dest])
    started = subprocess.run([server, "new", "-i", "127.0.0.1", "-X", receiver, "--", "/bin/sh", "-c",
                              "while :; do sleep 1000; done"],
                             env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=PATIENCE)
    connect = re.search(r"MOSH CONNECT (\d+) (\S+)", started.stdout)
    server_pid = re.search(r"pid = (\d+)", started.stderr)
    if not connect or not server_pid:
        sys.exit("FAIL REQ1/CP-6: the server did not start a stream's session:\n%s%s" % (started.stdout, started.stderr))
    try:
        sender = "\x1f".join(["@sh", " ".join(shlex.quote(w) for w in
                                              [sys.executable, os.path.abspath(__file__), "--liar", name, report,
                                               "send-anyway" if lying else "listen"])])
        subprocess.run([client, "-X", sender, "127.0.0.1", connect.group(1)],
                       env=dict(env, MOSH_KEY=connect.group(2)), stdin=subprocess.DEVNULL,
                       capture_output=True, timeout=PATIENCE * 2)
    finally:
        try:
            os.kill(int(server_pid.group(1)), signal.SIGTERM)  # (a stream's server waits a week for its client)
        except OSError:
            pass
    try:
        with open(report) as f:
            return [int(k) for k in f.read().split()]
    except OSError:
        return None  # the liar never got as far as writing down what it heard


def main():
    if len(sys.argv) == 5 and sys.argv[1] == "--liar":
        liar(sys.argv[2], sys.argv[3], sys.argv[4] == "send-anyway")
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    server, client = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    failures = []

    def check(name, ok, more=""):
        print("%-70s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else more), flush=True)
        if not ok:
            failures.append(name)

    work = tempfile.mkdtemp(prefix="gosh-copy-liar-")
    try:
        heard = offered(server, client, work, "inside.txt", False)
        check("an honest name: the receiver asks for the file", heard is not None and WANT in heard, repr(heard))

        outside = os.path.join(work, "outside.txt")
        for name in ("../outside.txt", "sub/../../outside.txt", outside):
            heard = offered(server, client, work, name, True)
            check('"%s": the receiver asks for nothing' % name.replace(work, "/…"),
                  heard is not None and WANT not in heard, repr(heard))
            strays = sorted(n for n in os.listdir(work) if n not in ("dest", "report"))
            check('"%s": and nothing is written outside the destination' % name.replace(work, "/…"), strays == [],
                  repr(strays))
            check('"%s": nor inside it' % name.replace(work, "/…"), os.listdir(os.path.join(work, "dest")) == [],
                  repr(os.listdir(os.path.join(work, "dest"))))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        sys.exit("FAIL REQ1/CP-6: %s" % "; ".join(failures))
    print("PASS REQ1/CP-6: a receiver given names outside its destination asks for nothing and writes nothing")


if __name__ == "__main__":
    main()
