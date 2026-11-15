#!/usr/bin/env python3
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""REQ15/PATH-4, REQ15/PATH-5: a meetup answers the STUN question, and passes datagrams for a ticket's two ends.

usage: meetup-tickets.py SERVER

Runs "SERVER meetup PORT" on loopback, with HOME and XDG_CONFIG_HOME set to a directory of
its own, so that the master key it makes is not anyone's real one. Gets a ticket from
"SERVER meetup-ticket", as a client end would over ssh, and plays both ends of it from two
sockets, sealing as an end does with a second implementation of the cipher (Python's
AES-OCB, RFC 7253), so that what gosh writes is read by code that is not gosh's.

  the STUN question     answered to anyone, with where the asker is
  a ticket's two ends   each registers, from where it is; a datagram from one goes to the other
  an onlooker           a REGISTER copied and sent from elsewhere gets nothing, and moves nothing
  the master key        made once, its owner's alone; --new makes another; a file others may
                        read is refused

If Python's cryptography package (AESOCB3) is not installed, the exchange is not tried, and
the test says so on a line of its own; the rest still runs. It stops only the meetups it
started, by process id.
"""
import base64
import os
import shutil
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time

if len(sys.argv) != 2:
    sys.exit(__doc__)
server = os.path.abspath(sys.argv[1])
failures = []

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESOCB3
except ImportError:
    AESOCB3 = None


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


def sock(family=socket.AF_INET):
    s = socket.socket(family, socket.SOCK_DGRAM)
    s.bind(("::1" if family == socket.AF_INET6 else "127.0.0.1", 0))
    s.settimeout(1.0)
    return s


def have_ipv6():
    try:
        s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        s.bind(("::1", 0))
        s.close()
        return True
    except OSError:
        return False


def receive(s, wait=1.0):
    s.settimeout(wait)
    try:
        return s.recv(4096)
    except OSError:
        return None


COOKIE = 0x2112A442
STUN = struct.pack(">HHI", 1, 0, COOKIE) + b"twelve bytes"
MARKS = {"REG": b"\0goshREG", "RGD": b"\0goshRGD", "PAS": b"\0goshPAS", "PSD": b"\0goshPSD"}
KINDS = {"REG": 1, "RGD": 2, "PAS": 3}


def where(s):
    port = s.getsockname()[1]
    if s.family == socket.AF_INET6:
        return bytes([6]) + socket.inet_pton(socket.AF_INET6, "::1") + struct.pack(">H", port)
    return bytes([4, 127, 0, 0, 1]) + bytes(12) + struct.pack(">H", port)


def seal(key, mark, ticket, end, from_meetup, counter, text):
    nonce = (((2 if from_meetup else 0) + end) << 62) | counter
    sealed = AESOCB3(key).encrypt(bytes(4) + struct.pack(">Q", nonce), bytes([KINDS[mark]]) + text, None)
    return MARKS[mark] + b"\x01" + ticket + bytes([end]) + struct.pack(">Q", nonce) + sealed


def open_registered(key, data, ticket, end):
    if not data or len(data) != 70 or data[:8] != MARKS["RGD"] or data[9:25] != ticket or data[25] != end:
        return None
    nonce = data[26:34]
    if struct.unpack(">Q", nonce)[0] >> 62 != 2 + end:
        return None
    text = AESOCB3(key).decrypt(bytes(4) + nonce, data[34:], None)
    return text[1:] if text[:1] == bytes([KINDS["RGD"]]) else None


home = tempfile.mkdtemp(prefix="gosh-meetup-test-")
env = dict(os.environ, LC_ALL="C.UTF-8", HOME=home, XDG_CONFIG_HOME=os.path.join(home, "config"))
keyfile = os.path.join(home, "config", "gosh", "meetup.key")
meetups = []


def start():
    port = free_port()
    m = subprocess.Popen([server, "meetup", str(port)], env=env, stdin=subprocess.DEVNULL, stderr=subprocess.PIPE)
    meetups.append(m)
    time.sleep(0.5)
    return m, port


def ticket():
    out = subprocess.run([server, "meetup-ticket"], env=env, capture_output=True, text=True, timeout=20)
    return out.stdout.strip(), out


try:
    text, out = ticket()
    check('"meetup-ticket" prints "gosh-ticket ID KEY"', out.returncode == 0 and len(text) == 57
          and text.startswith("gosh-ticket "), repr(out.stdout + out.stderr))
    mode = os.stat(keyfile).st_mode if os.path.exists(keyfile) else 0
    check("and has made the master key: 16 bytes, its owner's alone",
          os.path.exists(keyfile) and os.path.getsize(keyfile) == 16 and stat.S_IMODE(mode) == 0o600, oct(mode))
    check("a second ticket is another", ticket()[0] != text)

    meetup, port = start()
    check("the meetup starts and stays", meetup.poll() is None)
    asker = sock()
    asker.sendto(STUN, ("127.0.0.1", port))
    own = asker.getsockname()[1]
    expected = (struct.pack(">HHI", 0x0101, 12, COOKIE) + b"twelve bytes" + struct.pack(">HHBB", 0x0020, 8, 0, 1)
                + struct.pack(">H", own ^ (COOKIE >> 16)) + struct.pack(">I", 0x7F000001 ^ COOKIE))
    check("asked where the asker appears to be, by anyone, it says", receive(asker) == expected)
    asker.sendto(b"hello there, are you a meetup?", ("127.0.0.1", port))
    check("what is neither that nor one of its messages gets no answer", receive(asker, 0.5) is None)

    if AESOCB3 is None:
        print("SKIPPED: the exchange of a ticket's two ends: Python's cryptography package (AESOCB3) is not here",
              flush=True)
    else:
        tid = base64.b64decode(text[12:34] + "==")
        key = base64.b64decode(text[35:57] + "==")
        a, b, onlooker = sock(), sock(), sock()
        meet = ("127.0.0.1", port)

        a.sendto(seal(key, "REG", tid, 0, False, 0, where(a)), meet)
        check("end 0 registers, and the meetup's answer says where it was seen",
              open_registered(key, receive(a), tid, 0) == where(a))
        b.sendto(seal(key, "REG", tid, 1, False, 0, where(b)), meet)
        check("end 1 registers", open_registered(key, receive(b), tid, 1) == where(b))

        a.sendto(seal(key, "PAS", tid, 0, False, 1, b"hello from end 0"), meet)
        check("end 0's datagram reaches end 1, as PASSED", receive(b) == MARKS["PSD"] + b"\x01" + b"hello from end 0")
        b.sendto(seal(key, "PAS", tid, 1, False, 1, b"and back"), meet)
        check("and end 1's reaches end 0", receive(a) == MARKS["PSD"] + b"\x01" + b"and back")

        copied = seal(key, "REG", tid, 0, False, 2, where(a))
        onlooker.sendto(copied, meet)
        check("a REGISTER copied and sent from elsewhere gets nothing", receive(onlooker, 0.5) is None)
        b.sendto(seal(key, "PAS", tid, 1, False, 2, b"still yours"), meet)
        check("and end 1's datagrams still go to end 0", receive(a) == MARKS["PSD"] + b"\x01" + b"still yours")
        check("and none to the onlooker", receive(onlooker, 0.3) is None)

        wrong = bytes(16)
        onlooker.sendto(seal(wrong, "REG", tid, 0, False, 9, where(onlooker)), meet)
        check("a REGISTER sealed without the ticket's key gets nothing", receive(onlooker, 0.5) is None)

        # REQ16/PATH-8: the same, by IPv6, with a ticket of its own.
        if not have_ipv6():
            print("SKIPPED: the exchange by IPv6: this machine has no ::1", flush=True)
        else:
            text6, _ = ticket()
            tid6 = base64.b64decode(text6[12:34] + "==")
            key6 = base64.b64decode(text6[35:57] + "==")
            a6, b6 = sock(socket.AF_INET6), sock(socket.AF_INET6)
            meet6 = ("::1", port)
            a6.sendto(seal(key6, "REG", tid6, 0, False, 0, where(a6)), meet6)
            check("by IPv6: end 0 registers, and is told where it was seen (REQ16/PATH-8)",
                  open_registered(key6, receive(a6), tid6, 0) == where(a6))
            b6.sendto(seal(key6, "REG", tid6, 1, False, 0, where(b6)), meet6)
            check("by IPv6: end 1 registers", open_registered(key6, receive(b6), tid6, 1) == where(b6))
            a6.sendto(seal(key6, "PAS", tid6, 0, False, 1, b"by IPv6"), meet6)
            check("by IPv6: a datagram from end 0 reaches end 1", receive(b6) == MARKS["PSD"] + b"\x01" + b"by IPv6")
            asker6 = sock(socket.AF_INET6)
            asker6.sendto(STUN, meet6)
            got6 = receive(asker6)
            check("by IPv6: the STUN question is answered, 44 bytes", got6 is not None and len(got6) == 44, repr(got6))

        meetup.terminate()
        meetup.wait(timeout=5)
        new = subprocess.run([server, "meetup-key", "--new"], env=env, capture_output=True, text=True, timeout=20)
        check('"meetup-key --new" makes a new master key', new.returncode == 0, new.stderr)
        meetup, port = start()
        a.sendto(seal(key, "REG", tid, 0, False, 3, where(a)), ("127.0.0.1", port))
        check("and a ticket made under the old one gets nothing", receive(a, 0.5) is None)

    for m in meetups:
        if m.poll() is None:
            m.terminate()
            m.wait(timeout=5)
    os.chmod(keyfile, 0o644)
    bad = subprocess.run([server, "meetup", str(free_port())], env=env, stdin=subprocess.DEVNULL,
                         capture_output=True, text=True, timeout=20)
    check("with a master key others may read, it says so and does not start",
          bad.returncode == 1 and "chmod 600" in bad.stderr, repr(bad.stderr))
finally:
    for m in meetups:
        if m.poll() is None:
            m.terminate()
            try:
                m.wait(timeout=5)
            except subprocess.TimeoutExpired:
                m.kill()
    shutil.rmtree(home, ignore_errors=True)

if failures:
    sys.exit("FAIL REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: %s" % "; ".join(failures))
print("PASS REQ15/PATH-4, REQ15/PATH-5, REQ16/PATH-8: a meetup answers the question, and passes for a ticket's two ends alone")
