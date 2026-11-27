#!/usr/bin/env python3
"""
REQ6/SHAPE-5: for each proved domain, its text broken in the ways its proof is there to prevent,
and each proof seen to fail. A case is a copy of the domain with one edit, then the prover's prove.sh.
A class is broken in a copy of gosh's file: gosh is never written to. If gosh's text no longer has
the line a case breaks, the case fails, and says so.

Usage: tools/proofs.sh runs it, with DOMAIN (the domains put together in .build/proofs), SUBJECT
(gosh's src, there) and LEAN set. (Seen red 2026-11-18, in the project strong-domain-driven-design,
where it was written; about a minute.)
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOMAIN = os.environ["DOMAIN"]
PROVER = os.environ["PROVER"]       # tools/proofs.sh, which runs this, says which prover (tools/prover.commit)
PROVE = os.path.join(PROVER, "prove.sh")

# (the class, its file's text as it is, the text broken, what is owed that no longer holds)
# "CLASS:FILE" breaks FILE, one of the files the class is written with, and not the class's own.
CASES = [
    ("StunAnswer", "      if ( at + 4 + size > 20 + body ) {\n        break;\n      }\n", "",
     "an attribute's length is not tested against the body: a read outside the bytes"),
    ("StunAnswer", "type == 0x0020 && size >= 20 &&", "type == 0x0020 && size >= 4 &&",
     "an IPv6 value of 4 bytes is taken for 20: a read outside the bytes"),
    ("StunAnswer", "for ( uint64_t k = 0; k < 16; k++ )", "for ( uint64_t k = 0; k < 17; k++ )",
     "17 bytes are copied into 16: a write outside the address"),
    ("StunAnswer", "at = at + 4 + ( ( size + 3 ) & ~static_cast<uint64_t>( 3 ) );", "at = at + ( ( size + 3 ) & ~static_cast<uint64_t>( 3 ) );",
     "the loop does not move on: an attribute of no length would hold it for ever"),
    ("StunAnswer", "!( b0 == 0xfe && ( b1 & 0xc0 ) == 0x80 )", "!( b0 == 0xfd && ( b1 & 0xc0 ) == 0x80 )",
     "fd80 is tested where fe80 is meant: an address of one link alone is kept (the invariant)"),
    ("CounterWindow", "void clear( uint64_t c ) { seen[( c % WINDOW ) / 64] &=", "void clear( uint64_t c ) { seen[( c % WINDOW ) / 32] &=",
     "a word is chosen among 32 where there are 16: a write outside them"),
    ("CounterWindow", ">> ( c % 64 ) ) & 1;", ">> ( c % 128 ) ) & 1;",
     "a shift by up to 127 of 64 bits: undefined"),
    ("CounterWindow", "i <= c && i - top <= WINDOW; i++", "i <= c; i++",
     "the loop is not held to 1024 rounds: a counter far ahead would hold the meetup for up to 2^62 of them (it would still end: what fails is the proof that it ends within 1,026)"),
    ("CounterWindow", "static const uint64_t WINDOW = 1024;", "static const uint64_t WINDOW = 1025;",
     "a window of 1025 (the array is still 16 words): a remainder of 1024 chooses a seventeenth"),
    ("CounterWindow:src/strong/meetup/counter.h", "COUNTER_MOST = 0x3fffffffffffffff;", "COUNTER_MOST = 0xffffffffffffffff;",
     "a counter may be any 64-bit number: the window's proof, which rests on 2^62 - 1, no longer holds"),
    ("CounterWindow", "      top = c;\n      set( c );\n", "      top = c + COUNTER_MOST;\n      set( c );\n",
     "the highest counter kept is no longer one that was given: the window's invariant"),
    ("MeetupHeader", "if ( n < 9 || data.at( 8 ) != 2 ) {", "if ( n < 8 || data.at( 8 ) != 2 ) {",
     "eight bytes are taken for enough: the version, the ninth, is read outside them"),
    ("MeetupHeader", "if ( k == 5 && n == 67 ) { /* TIME */", "if ( k == 5 && n == 20 ) { /* TIME */",
     "a TIME of 20 bytes is let in: the end, at 25, is read outside it"),
    ("MeetupNonce", "counter = nonce & COUNTER_MOST;", "counter = nonce;",
     "a nonce's counter is read with the sender's two bits left in it: it is no longer of 62 bits (the invariant)"),
    ("MeetupRegister", "t = ( t << 8 ) | text.at( 26 );", "t = ( t << 8 ) | text.at( 27 );",
     "a byte past its 27 is read: a fixed window is read only below its length", "not written out below 27"),
    ("MeetupRegister", "if ( f != 4 && f != 6 ) {", "if ( f != 4 && f != 5 ) {",
     "a family of 5 is kept: no address has it (the invariant)"),
    ("MeetupRegistered", "uint32_t f = text.at( 0 );", "uint32_t f = text.at( 19 );",
     "a byte past its 19 is read: a fixed window is read only below its length", "not written out below 19"),
    ("MeetupRegistered", "if ( f != 4 && f != 6 ) {", "if ( f != 4 && f != 5 ) {",
     "a family of 5 is kept: no address has it (the invariant)"),
    ("MeetupPass", "if ( n < 1 || n > DATAGRAM_MOST ) {", "if ( n < 1 || n > DATAGRAM_MOST + 1 ) {",
     "a datagram of 1,501 bytes is kept: more than one may be (the invariant)"),
    ("MeetupPass", "length = n;", "length = n + text.at( n );",
     "the byte just past the text is read"),
    ("MeetupTime", "t = ( t << 8 ) | text.at( 15 );", "t = ( t << 8 ) | text.at( 16 );",
     "a byte past its 16 is read: a fixed window is read only below its length", "not written out below 16"),
    ("MeetupTime", "if ( a > COUNTER_MOST ) {", "if ( a > COUNTER_MOST + 1 ) {",
     "a counter of 2^62 is kept: no REGISTER has it (the invariant)"),
    ("is_stun_request_checked", "request.size() < 20 ||", "request.size() < 7 ||",
     "seven bytes are taken for enough: the eighth is read outside them"),
]

# RED_ONLY="A B": only the cases of these domains (the check's --lite); the rules' own cases are then left out.
ONLY = set(os.environ.get("RED_ONLY", "").split())


def wanted(name):
    return not ONLY or name in ONLY


failures = 0
ran = 0
for case in CASES:
    klass, old, new, what = case[:4]
    expect = case[4] if len(case) > 4 else "a proof does not hold"     # or a refusal, where the break is not even modelled
    klass, _, other = klass.partition(":")
    if not wanted(klass):
        continue
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "domain")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        env = dict(os.environ, DOMAIN=work)
        path = os.path.join(work, "model", klass + ".hpp")
        names = [klass]
        if os.path.exists(os.path.join(work, "from", klass)):       # gosh's own file: a copy of it
            said = open(os.path.join(work, "from", klass)).read().split("\n")
            rel = said[0].split()[0]                                  # (a second word says "function")
            names = [x[5:] for x in said if x.startswith("uses ")] + [klass]     # what it uses is proved first
            files = [open(os.path.join(work, "from", name)).read().split()[0] for name in names]
            files += [x.split()[1] for x in said if x.startswith("needs ")]      # and the names it is written with
            for r1 in files:                                          # each one's file, and nothing else of gosh
                os.makedirs(os.path.dirname(os.path.join(d, "subject", r1)), exist_ok=True)
                shutil.copy(os.path.join(os.environ["SUBJECT"], r1), os.path.join(d, "subject", r1))
            path = os.path.join(d, "subject", other or rel)
            env["SUBJECT"] = os.path.join(d, "subject")
        text = open(path).read()
        if text.count(old) != 1:
            print("red: %s: FAIL: the text this case breaks is there %d times, not once: %r" % (klass, text.count(old), old))
            failures += 1
            continue
        open(path, "w").write(text.replace(old, new))
        r = subprocess.run(["sh", PROVE] + names, capture_output=True, text=True, env=env)
        out = r.stdout + r.stderr
        ok = r.returncode != 0 and expect in out if len(case) > 4 else r.returncode == 1 and expect in out
        print("red: %s: %s: %s" % (klass, what, ("the proof fails, ok" if len(case) <= 4 else "refused, ok") if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-200:])))
        failures += not ok
        ran += 1

# REQ6/SHAPE-5 (strong-domain-driven-design: REQ1/SHAPE-4): code that is not proved, making a counter itself where it is to hand over a checked
# one: the rule refuses the program. In a copy of gosh's src, with what the check just generated.
INGRESS = os.path.join(PROVER, "ingress.py")
if not ONLY:
    with tempfile.TemporaryDirectory() as d:
        shutil.copytree(os.environ["SUBJECT"], os.path.join(d, "subject"))
        path = os.path.join(d, "subject", "src", "meetup", "imp", "registrations.h")
        text = open(path).read()
        old, new = "self.counters.take_checked( Strong::Meetup::CounterChecked( ", "self.counters.take( Strong::Meetup::Counter( "
        names = sorted(os.listdir(os.path.join(DOMAIN, "from")))
        if text.count(old) != 2:
            print("red: ingress: FAIL: the text this case breaks is there %d times, not twice" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, new))
            r = subprocess.run([sys.executable, INGRESS] + names, capture_output=True, text=True,
                               env=dict(os.environ, DOMAIN=DOMAIN, SUBJECT=os.path.join(d, "subject"), STRONG="src/strong"))
            ok = r.returncode == 1 and r.stdout.count("makes a Counter") == 2
            print("red: ingress: Registrations, which is not proved, makes a counter itself, tested by nothing: %s"
                  % ("the program is refused, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, r.stdout.strip()[-300:])))
            failures += not ok
            ran += 1
        # and a call of what is proved by a name that is not `_checked`, though nothing there could fail
        path = os.path.join(d, "subject", "src", "stun", "stun.h")
        text = open(path).read()
        old, new = "if ( answer.kept_family_checked() == 4 ) {", "if ( answer.invariant() && answer.kept_family_checked() == 4 ) {"
        if text.count(old) != 1:
            print("red: ingress: FAIL: the text the second case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, new))
            r = subprocess.run([sys.executable, INGRESS] + names, capture_output=True, text=True,
                               env=dict(os.environ, DOMAIN=DOMAIN, SUBJECT=os.path.join(d, "subject"), STRONG="src/strong"))
            ok = r.returncode == 1 and "calls invariant on answer" in r.stdout
            print("red: ingress: stun_parse, which is not proved, calls a proved method by a name that is not _checked: %s"
                  % ("the program is refused, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, r.stdout.strip()[-300:])))
            failures += not ok
            ran += 1
# REQ6/SHAPE-5 (strong-domain-driven-design: REQ1/SHAPE-4): code that is not proved reads a window on an array itself, where it may only make
# one and hand it over: the lint refuses the program. In a copy of what is proved and the one file that calls it (the
# whole of gosh's src was linted as it is by the check, just before).
LINT = os.path.join(PROVER, "lint.py")
if not ONLY:
    with tempfile.TemporaryDirectory() as d:
        shutil.copytree(os.path.join(os.environ["SUBJECT"], "src", "strong"), os.path.join(d, "subject", "src", "strong"))
        os.makedirs(os.path.join(d, "subject", "src", "stun"))
        path = os.path.join(d, "subject", "src", "stun", "stun.h")
        text = open(os.path.join(os.environ["SUBJECT"], "src", "stun", "stun.h")).read()
        old = "answer.parse_checked( Strong::Datagram( reinterpret_cast<const unsigned char*>( data ), len ) )"
        new = old + " || Strong::Datagram( reinterpret_cast<const unsigned char*>( data ), len ).at( 0 ) != 1"
        env = dict(os.environ, SUBJECT=os.path.join(d, "subject"), STRONG="src/strong", INCLUDE=os.environ.get("LINT_INCLUDE", ""))
        open(path, "w").write(text)
        r = subprocess.run([sys.executable, LINT], capture_output=True, text=True, env=env)
        if text.count(old) != 1 or r.returncode != 0:
            print("red: lint: FAIL: the text this case breaks is there %d times, not once; or the two files alone are not accepted (exit %d) %s"
                  % (text.count(old), r.returncode, r.stdout.strip()[-300:]))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, new))
            r = subprocess.run([sys.executable, LINT], capture_output=True, text=True, env=env)
            ok = r.returncode == 1 and "at is called of a window" in r.stdout
            print("red: lint: stun_parse, which is not proved, reads a byte of a datagram itself, by at(), which tests nothing: %s"
                  % ("the program is refused, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, r.stdout.strip()[-300:])))
            failures += not ok
            ran += 1
# REQ6/SHAPE-5: a domain reaches another's inside where the map does not allow it: the lint refuses the program. In a
# copy of all of gosh's src, relay's fuzzer is made to include path-finding's imp/finding.h and use a name of
# Gosh::Pathfinding::Imp (PROBE_LEN); the whole was linted as it is by the check, just before.
if not ONLY:
    with tempfile.TemporaryDirectory() as d:
        shutil.copytree(os.environ["SUBJECT"], os.path.join(d, "subject"))
        path = os.path.join(d, "subject", "src", "relay", "imp", "frame-fuzzer.cc")
        text = open(path).read()
        old_include = '#include "src/pathfinding/mark.h"\n'
        old_use = "  const std::string mark( Gosh::Pathfinding::PUNCH_MARK, sizeof( Gosh::Pathfinding::PUNCH_MARK ) );\n"
        if text.count(old_include) != 1 or text.count(old_use) != 1:
            print("red: a domain: FAIL: the text this case changes is not there once")
            failures += 1
        else:
            text = text.replace(old_include, old_include + '#include "src/pathfinding/imp/finding.h"\n')
            text = text.replace(old_use, old_use + "  (void)Gosh::Pathfinding::Imp::PathFinding::PROBE_LEN;\n")
            open(path, "w").write(text)
            env = dict(os.environ, SUBJECT=os.path.join(d, "subject"), STRONG="src/strong", INCLUDE=os.environ.get("LINT_INCLUDE", ""), DOMAINS="src/DOMAINS")
            r = subprocess.run([sys.executable, LINT], capture_output=True, text=True, env=env)
            ok = r.returncode == 1 and "src/relay/imp/frame-fuzzer.cc:57: uses PROBE_LEN, of Gosh::Pathfinding's inside" in r.stdout \
                and "src/relay/imp/frame-fuzzer.cc:26: includes src/pathfinding/imp/finding.h, of Gosh::Pathfinding's inside" in r.stdout
            print("red: a domain: relay's fuzzer includes path-finding's imp/ and uses a name of Gosh::Pathfinding::Imp, and the map does not allow it: %s"
                  % ("the program is refused, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, r.stdout.strip()[-300:])))
            failures += not ok
            ran += 1
# REQ21/SHAPE-1: the core's network reaches the cipher's inside, where the core's map (seam/DOMAINS) does not
# allow it: the lint of the core as built refuses it. In a copy of .build/core's code, network's imp/network.cc is made to
# include crypto's imp/base64.h and use Crypto::Imp::base64_encode; the whole was linted as it is, just before.
if not ONLY:
    with tempfile.TemporaryDirectory() as d:
        core = os.environ["CORE"]
        shutil.copytree(os.path.join(core, "src"), os.path.join(d, "core", "src"),
                        ignore=lambda where, names: [n for n in names if not os.path.isdir(os.path.join(where, n)) and not n.endswith((".cc", ".cpp", ".h", ".hpp"))])
        shutil.copytree(os.path.join(core, "vendor"), os.path.join(d, "core", "vendor"))
        path = os.path.join(d, "core", "src", "network", "imp", "network.cc")
        text = open(path).read()
        old_include = '#include "src/crypto/crypto.h"\n'
        if text.count(old_include) != 1:
            print("red: the core's domains: FAIL: the text this case changes is not there once")
            failures += 1
        else:
            text = text.replace(old_include, old_include + '#include "src/crypto/imp/base64.h"\n')
            text += "\nvoid* crossing( void ) { return reinterpret_cast<void*>( &Crypto::Imp::base64_encode ); }\n"
            open(path, "w").write(text)
            include = os.environ.get("CORE_LINT_INCLUDE", "").replace(core, os.path.join(d, "core"))
            env = dict(os.environ, SUBJECT=os.path.join(d, "core"), STRONG="src/strong", RULES="a domain", INCLUDE=include,
                       FLAGS=os.environ.get("CORE_LINT_FLAGS", ""), DOMAINS=os.path.abspath("seam/DOMAINS"))
            r = subprocess.run([sys.executable, LINT], capture_output=True, text=True, env=env)
            ok = r.returncode == 1 and ": uses base64_encode, of Crypto's inside" in r.stdout \
                and ": includes src/crypto/imp/base64.h, of Crypto's inside" in r.stdout and "src/network/imp/network.cc:" in r.stdout
            print("red: the core's domains: the core's network includes the cipher's imp/ and uses a name of Crypto::Imp, and the map does not allow it: %s"
                  % ("the program is refused, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, r.stdout.strip()[-300:])))
            failures += not ok
            ran += 1

# REQ6/SHAPE-5 (strong-domain-driven-design: REQ1/SHAPE-3): what is claimed of StunAnswer in C++ (proofs/StunAnswer.proof.cpp) claims more than is
# so: that nothing kept is IPv6. The proof file is not proved.
if wanted("StunAnswer"):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "gosh")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        path = os.path.join(work, "proofs", "StunAnswer.proof.cpp")
        text = open(path).read()
        old = " || answer.kept_family_checked() == 6 );"
        if text.count(old) != 1:
            print("red: proof file: FAIL: the text this case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, " );"))
            r = subprocess.run(["sh", PROVE, "StunAnswer"], capture_output=True, text=True, env=dict(os.environ, DOMAIN=work))
            out = r.stdout + r.stderr
            ok = r.returncode == 1 and "a PROVE of proofs/StunAnswer.proof.cpp is not proved" in out
            print("red: proof file: StunAnswer's proof file says nothing kept is IPv6, which is not so: %s"
                  % ("it is not proved, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-300:])))
            failures += not ok
            ran += 1
# The same of MeetupHeader's proof file: it says a head with a ticket is of 53 bytes or more, and one of 52 is read.
if wanted("MeetupHeader"):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "gosh")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        path = os.path.join(work, "proofs", "MeetupHeader.proof.cpp")
        text = open(path).read()
        old = "head.whole_checked() >= 52 );"
        if text.count(old) != 1:
            print("red: proof file: FAIL: the text this case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, "head.whole_checked() >= 53 );"))
            r = subprocess.run(["sh", PROVE, "MeetupHeader"], capture_output=True, text=True, env=dict(os.environ, DOMAIN=work))
            out = r.stdout + r.stderr
            ok = r.returncode == 1 and "a PROVE of proofs/MeetupHeader.proof.cpp is not proved" in out
            print("red: proof file: MeetupHeader's proof file says a head with a ticket is of 53 bytes or more, and a PASS of 52 is read: %s"
                  % ("it is not proved, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-300:])))
            failures += not ok
            ran += 1
# The same of MeetupNonce's proof file: it says of three ends, not two, that each is read again as itself; a third is read as the second.
if wanted("MeetupNonce"):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "gosh")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        path = os.path.join(work, "proofs", "MeetupNonce.proof.cpp")
        text = open(path).read()
        old = "for ( uint32_t an_end : all<uint32_t>( 0, 1 ) ) {"
        if text.count(old) != 1:
            print("red: proof file: FAIL: the text this case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, "for ( uint32_t an_end : all<uint32_t>( 0, 2 ) ) {"))
            r = subprocess.run(["sh", PROVE, "MeetupNonce"], capture_output=True, text=True, env=dict(os.environ, DOMAIN=work))
            out = r.stdout + r.stderr
            ok = r.returncode == 1 and "a PROVE of proofs/MeetupNonce.proof.cpp is not proved" in out
            print("red: proof file: MeetupNonce's proof file says a third end is read again as itself, and there are two: %s"
                  % ("it is not proved, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-300:])))
            failures += not ok
            ran += 1
# The same of MeetupRegister's proof file: it says a family read is 0 or 4, and one of 6 is read.
if wanted("MeetupRegister"):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "gosh")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        path = os.path.join(work, "proofs", "MeetupRegister.proof.cpp")
        text = open(path).read()
        old = "PROVE( r.family_checked() == 0 || r.family_checked() == 4 || r.family_checked() == 6 );"
        if text.count(old) != 1:
            print("red: proof file: FAIL: the text this case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, "PROVE( r.family_checked() == 0 || r.family_checked() == 4 );"))
            r = subprocess.run(["sh", PROVE, "MeetupRegister"], capture_output=True, text=True, env=dict(os.environ, DOMAIN=work))
            out = r.stdout + r.stderr
            ok = r.returncode == 1 and "a PROVE of proofs/MeetupRegister.proof.cpp is not proved" in out
            print("red: proof file: MeetupRegister's proof file says a family read is 0 or 4, and one of 6 is: %s"
                  % ("it is not proved, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-300:])))
            failures += not ok
            ran += 1
# The same of MeetupTime's proof file: it says a counter kept is under 2^62 - 1, and 2^62 - 1 is kept.
if wanted("MeetupTime"):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "gosh")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        path = os.path.join(work, "proofs", "MeetupTime.proof.cpp")
        text = open(path).read()
        old = "<= 0x3fffffffffffffffULL );"
        if text.count(old) != 1:
            print("red: proof file: FAIL: the text this case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, "<= 0x3ffffffffffffffeULL );"))
            r = subprocess.run(["sh", PROVE, "MeetupTime"], capture_output=True, text=True, env=dict(os.environ, DOMAIN=work))
            out = r.stdout + r.stderr
            ok = r.returncode == 1 and "a PROVE of proofs/MeetupTime.proof.cpp is not proved" in out
            print("red: proof file: MeetupTime's proof file says a counter kept is under 2^62 - 1, and 2^62 - 1 is: %s"
                  % ("it is not proved, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-300:])))
            failures += not ok
            ran += 1
# The same of MeetupRegistered's proof file: it says a family read is 0 or 4, and one of 6 is read.
if wanted("MeetupRegistered"):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "gosh")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        path = os.path.join(work, "proofs", "MeetupRegistered.proof.cpp")
        text = open(path).read()
        old = "|| r.family_checked() == 4 || r.family_checked() == 6 );"
        if text.count(old) != 1:
            print("red: proof file: FAIL: the text this case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, "|| r.family_checked() == 4 );"))
            r = subprocess.run(["sh", PROVE, "MeetupRegistered"], capture_output=True, text=True, env=dict(os.environ, DOMAIN=work))
            out = r.stdout + r.stderr
            ok = r.returncode == 1 and "a PROVE of proofs/MeetupRegistered.proof.cpp is not proved" in out
            print("red: proof file: MeetupRegistered's proof file says a family read is 0 or 4, and one of 6 is: %s"
                  % ("it is not proved, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-300:])))
            failures += not ok
            ran += 1
# The same of MeetupPass's proof file: it says a length kept is at most 1,499, and 1,500 is kept.
if wanted("MeetupPass"):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, "gosh")
        shutil.copytree(DOMAIN, work, ignore=shutil.ignore_patterns("build", "gen"))
        path = os.path.join(work, "proofs", "MeetupPass.proof.cpp")
        text = open(path).read()
        old = "PROVE( p.length_checked() <= 1500 );"
        if text.count(old) != 1:
            print("red: proof file: FAIL: the text this case breaks is there %d times, not once" % text.count(old))
            failures += 1
        else:
            open(path, "w").write(text.replace(old, "PROVE( p.length_checked() <= 1499 );"))
            r = subprocess.run(["sh", PROVE, "MeetupPass"], capture_output=True, text=True, env=dict(os.environ, DOMAIN=work))
            out = r.stdout + r.stderr
            ok = r.returncode == 1 and "a PROVE of proofs/MeetupPass.proof.cpp is not proved" in out
            print("red: proof file: MeetupPass's proof file says a length kept is at most 1,499, and 1,500 is: %s"
                  % ("it is not proved, ok" if ok else "FAIL (exit %d) %s" % (r.returncode, out.strip()[-300:])))
            failures += not ok
            ran += 1
print("red: FAIL: %d" % failures if failures else "red: PASS (%d cases%s)" % (ran, ", of %s alone" % " ".join(sorted(ONLY)) if ONLY else ""))
sys.exit(1 if failures else 0)
