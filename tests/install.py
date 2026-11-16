#!/usr/bin/env python3
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""REQ1/CMD-2, REQ17/CMD-4: the installer, from a release made here, into a home of the test's own.

usage: install.py INSTALL_SH GOSH_CLIENT GOSH_SERVER

A release is made from the two programs in a temporary directory, as the release workflow lays it
out (vVERSION/gosh-VERSION-OS-ARCH.tar.gz and SHA256SUMS), and fetched by file://. Each case has a
home and a PATH of its own: only the tools the installer needs, and the mosh a case says. Nothing
is installed anywhere but the test's directory, and nothing is asked of the Internet.

  no mosh on the PATH        gosh-client, gosh-server, gosh, gocp; and mosh-client, mosh-server
                             as links to gosh's; the installed gosh runs
  installed again            the same, over the first: its own links kept
  a mosh on the PATH         no mosh-client or mosh-server link, and it says whose is left
  a wrong checksum           refused, status 1, nothing installed
  a client that does not run refused, status 1, nothing installed
  no curl and no wget        refused, status 1, and says so
"""
import hashlib
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

install_sh, client, server = (os.path.abspath(p) for p in sys.argv[1:4])
failures = []


def check(name, ok, more=""):
    print("%-72s %s %s" % (name, "ok" if ok else "FAIL", "" if ok else more), flush=True)
    if not ok:
        failures.append(name)


work = tempfile.mkdtemp(prefix="gosh-install-")
VERSION = "0.0.test"
os_name = {"Linux": "linux", "Darwin": "macos"}[platform.system()]
arch = "universal" if os_name == "macos" else {"x86_64": "x86_64", "amd64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}[platform.machine()]
NAME = "gosh-%s-%s-%s" % (VERSION, os_name, arch)


def release(where, programs, sums_right=True):
    """A release directory: where/vVERSION/NAME.tar.gz and SHA256SUMS. programs: name -> file."""
    stage = os.path.join(where, "stage", NAME)
    os.makedirs(os.path.join(stage, "bin"))
    os.makedirs(os.path.join(stage, "share", "man", "man1"))
    for name, path in programs.items():
        shutil.copy(path, os.path.join(stage, "bin", name))
    with open(os.path.join(stage, "share", "man", "man1", "gosh.1"), "w") as f:
        f.write(".TH GOSH 1\n")
    out = os.path.join(where, "v" + VERSION)
    os.makedirs(out)
    tarball = os.path.join(out, NAME + ".tar.gz")
    with tarfile.open(tarball, "w:gz") as t:
        t.add(stage, arcname=NAME)
    digest = hashlib.sha256(open(tarball, "rb").read()).hexdigest()
    if not sums_right:
        digest = ("0" if digest[0] != "0" else "1") + digest[1:]
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        f.write("%s  %s.tar.gz\n" % (digest, NAME))
    return "file://" + where


# The tools the installer uses, and nothing else: so that no mosh is found unless a case puts one.
tools = os.path.join(work, "tools")
os.mkdir(tools)
NEEDED = ["sh", "uname", "mktemp", "rm", "sed", "head", "cut", "tar", "gzip", "mkdir", "cp", "chmod", "mv",
          "ln", "readlink", "sha256sum", "curl"]
for tool in NEEDED:
    found = shutil.which(tool)
    if found:
        os.symlink(found, os.path.join(tools, tool))
no_fetch = os.path.join(work, "no-fetch")  # the same, less curl and wget
os.mkdir(no_fetch)
for tool in NEEDED:
    if tool not in ("curl", "wget") and shutil.which(tool):
        os.symlink(shutil.which(tool), os.path.join(no_fetch, tool))
a_mosh = os.path.join(work, "a-mosh")  # a mosh of the user's own, elsewhere on the PATH
os.mkdir(a_mosh)
for name in ("mosh-client", "mosh-server"):
    with open(os.path.join(a_mosh, name), "w") as f:
        f.write("#!/bin/sh\necho a mosh of the user's own\n")
    os.chmod(os.path.join(a_mosh, name), 0o755)

good = release(os.path.join(work, "good"), {"gosh-client": client, "gosh-server": server})
bad_sums = release(os.path.join(work, "bad-sums"), {"gosh-client": client, "gosh-server": server}, sums_right=False)
broken_client = os.path.join(work, "broken-client")
with open(broken_client, "w") as f:
    f.write("#!/bin/sh\nexit 1\n")
os.chmod(broken_client, 0o755)
broken = release(os.path.join(work, "broken"), {"gosh-client": broken_client, "gosh-server": server})
broken_server = release(os.path.join(work, "broken-server"), {"gosh-client": client, "gosh-server": broken_client})
# a later release, as far as the installer can tell: the same client with bytes after its end (it still runs)
later_client = os.path.join(work, "later-client")
with open(later_client, "wb") as f:
    f.write(open(client, "rb").read() + b"\0later release\0")
os.chmod(later_client, 0o755)
later = release(os.path.join(work, "later"), {"gosh-client": later_client, "gosh-server": server})


def install(releases, path, home=None, prefix=None):
    home = home or tempfile.mkdtemp(prefix="home-", dir=work)
    env = {"HOME": home, "PATH": path, "GOSH_VERSION": VERSION, "GOSH_RELEASES": releases, "LC_ALL": "C"}
    if prefix is not None:
        env["PREFIX"] = prefix
    done = subprocess.run(["sh", install_sh], env=env, capture_output=True, text=True, timeout=120)
    return done, home


def binned(home):
    where = os.path.join(home, ".local", "bin")
    return sorted(os.listdir(where)) if os.path.isdir(where) else []


def link(home, name):
    try:
        return os.readlink(os.path.join(home, ".local", "bin", name))
    except OSError:
        return None


try:
    # ---- no mosh on the PATH
    done, home = install(good, tools)
    check("no mosh: it installs, with status 0", done.returncode == 0, done.stdout + done.stderr)
    check("no mosh: gosh-client, gosh-server, gosh, gocp, mosh-client, mosh-server",
          binned(home) == ["gocp", "gosh", "gosh-client", "gosh-server", "mosh-client", "mosh-server"], repr(binned(home)))
    check("no mosh: gosh and gocp are gosh-client by other names",
          link(home, "gosh") == "gosh-client" and link(home, "gocp") == "gosh-client")
    check("no mosh: mosh-client and mosh-server are links to gosh's (REQ17/CMD-4)",
          link(home, "mosh-client") == "gosh-client" and link(home, "mosh-server") == "gosh-server")
    ran = subprocess.run([os.path.join(home, ".local", "bin", "gosh-server"), "--version"], capture_output=True, text=True)
    check("no mosh: the installed server runs", ran.returncode == 0 and "mosh" in ran.stdout, ran.stdout + ran.stderr)
    check("no mosh: the installed server says which gosh it is, then which mosh",
          re.match(r"gosh-server, of gosh [^\s(]+\n", ran.stdout) is not None and "unknown" not in ran.stdout.split("\n")[0],
          ran.stdout)
    check("no mosh: and so does what it says it installed", re.search(r"Installed gosh-client, of gosh \S+ in ", done.stdout) is not None,
          done.stdout)
    check("no mosh: the man page is installed", os.path.isfile(os.path.join(home, ".local", "share", "man", "man1", "gosh.1")))
    check("no mosh: it says it is not independently reviewed", "Not independently reviewed" in done.stdout, done.stdout)

    # ---- installed again, over the first, with its bin on the PATH
    again, _ = install(good, os.path.join(home, ".local", "bin") + ":" + tools, home=home)
    check("installed again: status 0, and its own mosh links are kept",
          again.returncode == 0 and link(home, "mosh-client") == "gosh-client" and link(home, "mosh-server") == "gosh-server",
          again.stdout + again.stderr)
    check("installed again: its own links are not taken for a mosh of the user's", "is left as it is" not in again.stdout,
          again.stdout)

    # ---- a mosh already on the PATH
    done, home = install(good, a_mosh + ":" + tools)
    check("a mosh on the PATH: it installs, with status 0", done.returncode == 0, done.stdout + done.stderr)
    check("a mosh on the PATH: no mosh-client or mosh-server of gosh's (REQ17/CMD-4)",
          binned(home) == ["gocp", "gosh", "gosh-client", "gosh-server"], repr(binned(home)))
    check("a mosh on the PATH: it says whose is left",
          ("mosh-server: yours at %s/mosh-server is left as it is" % a_mosh) in done.stdout, done.stdout)

    # ---- the user's own programs in ~/.local/bin, which is not on the PATH (the usual case on a host
    # reached by ssh): never replaced (REQ17/CMD-4; the review of item 10, 2026-11-16)
    home = tempfile.mkdtemp(prefix="home-", dir=work)
    own = os.path.join(home, ".local", "bin")
    os.makedirs(own)
    for name in ("mosh-client", "mosh-server", "gosh"):
        with open(os.path.join(own, name), "w") as f:
            f.write("#!/bin/sh\necho the user's own %s\n" % name)
        os.chmod(os.path.join(own, name), 0o755)
    done, _ = install(good, tools, home=home)
    kept = all(open(os.path.join(own, n), "rb").read() == ("#!/bin/sh\necho the user's own %s\n" % n).encode()
               for n in ("mosh-client", "mosh-server", "gosh"))
    check("the user's own in ~/.local/bin, off the PATH: status 0, and none of them replaced",
          done.returncode == 0 and kept and link(home, "gocp") == "gosh-client", done.stdout + done.stderr)
    check("and it says whose each is", all(("%s: yours at %s/%s is left as it is" % (n, own, n)) in done.stdout
                                           for n in ("mosh-client", "mosh-server")), done.stdout)
    check("and how to run gosh when gosh is another program's",
          'gosh: %s/gosh is another program, left as it is; run gosh as "gosh-client gosh ..."' % own in done.stdout,
          done.stdout)

    # ---- PREFIX with a trailing slash, installed again, with its bin on the PATH
    home = tempfile.mkdtemp(prefix="home-", dir=work)
    install(good, tools, home=home)
    again, _ = install(good, os.path.join(home, ".local", "bin") + ":" + tools, home=home,
                       prefix=os.path.join(home, ".local") + "/")
    check("PREFIX ending in a slash: its own links kept, and not taken for the user's",
          again.returncode == 0 and link(home, "mosh-server") == "gosh-server" and "is left as it is" not in again.stdout,
          again.stdout + again.stderr)
    check("PREFIX ending in a slash: its bin is known to be on the PATH", "is not on your PATH" not in again.stdout,
          again.stdout)

    # ---- a copy that fails midway: the old pair is left as it was, with nothing staged left behind
    home = tempfile.mkdtemp(prefix="home-", dir=work)
    install(good, tools, home=home)
    bin_dir = os.path.join(home, ".local", "bin")
    before = open(os.path.join(bin_dir, "gosh-client"), "rb").read()
    blocker = os.path.join(bin_dir, ".gosh-server.new")
    os.mkdir(blocker)
    os.chmod(blocker, 0o500)  # gosh-server cannot be staged here
    done, _ = install(later, tools, home=home)
    os.chmod(blocker, 0o700)
    os.rmdir(blocker)
    check("a copy that fails midway: status not 0, gosh-client not replaced alone, nothing staged left",
          done.returncode != 0 and open(os.path.join(bin_dir, "gosh-client"), "rb").read() == before
          and not os.path.exists(os.path.join(bin_dir, ".gosh-client.new")), done.stdout + done.stderr)

    # ---- refusals
    done, home = install(broken_server, tools)
    check("a server that does not run: refused, status 1, nothing installed",
          done.returncode == 1 and "gosh-server does not run here" in done.stderr and binned(home) == [], done.stderr)
    done, home = install(bad_sums, tools)
    check("a wrong checksum: refused, status 1, nothing installed",
          done.returncode == 1 and "is not what SHA256SUMS says" in done.stderr and binned(home) == [], done.stderr)
    done, home = install(broken, tools)
    check("a client that does not run: refused, status 1, nothing installed",
          done.returncode == 1 and "does not run here" in done.stderr and binned(home) == [], done.stderr)
    done, home = install(good, no_fetch)
    check("no curl and no wget: refused, status 1, and says so",
          done.returncode == 1 and "need curl or wget" in done.stderr and binned(home) == [], done.stderr)
finally:
    shutil.rmtree(work, ignore_errors=True)

if failures:
    sys.exit("FAIL REQ1/CMD-2, REQ17/CMD-4: %s" % "; ".join(failures))
print("PASS REQ1/CMD-2, REQ17/CMD-4: the installer checks what it fetched, and links mosh's names only where none is found")
