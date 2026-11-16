#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
# The full check: the one command a person, an agent and CI all run (SE1/O3).
# Prints PASS and exits 0, or says what failed and exits 1. In order:
#   1. the repository is sound (se --doctor);
#   2. the core is upstream's at the pinned commit, byte for byte (REQ6/UP-2);
#   3. it builds (tools/build.sh), with the seam's patches applied to a copy of
#      the core and gosh's parts beside it, and the parts' unit tests pass;
#   4. gosh and a stock mosh hold a terminal session, each way (REQ10/SHAPE-1);
#   5. the fork's tests that drive the programs from outside pass against the
#      fork's own programs at pf.7 (tools/build-pf7.sh). These are the tests each
#      part of gosh must then pass; until a part lands they say only that the
#      test and the reference agree (queue item 4);
#   6. gosh's own programs pass the ones they can so far, and a gosh end works
#      with a pf.7 end each way.
# A stock mosh 1.4.0 must be installed for 4 (Debian, Ubuntu: the mosh package);
# STOCK_CLIENT and STOCK_SERVER say where, if not in /usr/bin.
cd "$(dirname "$0")/.." || exit 1
GOSH=$(pwd); export GOSH
# Everything this run starts carries this mark, so that what it stops is its own and not
# another run's from the same build directory (tools/env.sh, stop_ours; the tests).
GOSH_TEST_RUN="check.$$"; export GOSH_TEST_RUN
fail() { echo "FAIL: $*"; exit 1; }

# The doctor asks the network whether its guide is current. On a machine that has
# never been able to ask (a fresh CI runner, with --offline) it fails rather than
# say "current" unasked, so it is left to ask. So on such a machine this check
# needs api.github.com to answer; when it does not, the line is
# "[fail] release check ... not made", and the remedy is to run it again.
python3 se.py --doctor || fail "se --doctor"

# REQ6/UP-2. The pin is the commit this repository records for core/ (git's own
# entry for the submodule, so it is written in one place). The core is right when
# it is checked out at exactly that commit and no file in it differs.
pin=$(git ls-files -s core | awk '{print $2}')
[ -n "$pin" ] || fail "REQ6/UP-2: this repository records no pinned commit for core/"
[ "$(git -C core rev-parse HEAD 2>/dev/null)" = "$pin" ] \
    || fail "REQ6/UP-2: core/ is not at the pinned commit $pin (git submodule update --init)"
[ -z "$(git -C core status --porcelain)" ] \
    || fail "REQ6/UP-2: core/ differs from upstream at $pin: $(git -C core status --porcelain | head -3)"
echo "core: upstream at $pin, untouched (REQ6/UP-2)"

tools/build.sh || fail "tools/build.sh"

# The unit tests of gosh's parts, built and run inside the build's copy; and each part's fuzzers,
# which replay their corpora and a seeded search around them (queue item 21; tools/fuzz.sh runs
# them under libFuzzer where there is clang).
. tools/env.sh
# (every part there is: the directories of src/, as tools/fuzz.sh and tools/asan-replay.sh find their fuzzers)
for part in $(cd src && ls -d */ | tr -d /); do
    if make -C ".build/core/src/$part" check >.build/unit-$part.log 2>&1; then
        echo "unit tests pass: $part ($(grep -c '^PASS:' .build/unit-$part.log))"
    else
        grep -E '^(FAIL|ERROR):|error' .build/unit-$part.log | head -10; fail "unit tests of $part (log: .build/unit-$part.log)"
    fi
done
# The same fuzzers again under AddressSanitizer, in a build of their own (.build/asan): a read past
# a buffer's end that happens not to crash passes the replay above (the review of item 21). About
# a minute and a half.
tools/asan-replay.sh || fail "the fuzzers under AddressSanitizer (tools/asan-replay.sh)"

stock_client=${STOCK_CLIENT:-/usr/bin/mosh-client}
stock_server=${STOCK_SERVER:-/usr/bin/mosh-server}
python3 tests/stock-session.py .build/bin/gosh-client "$stock_server" || fail "gosh's client with a stock server"
python3 tests/stock-session.py "$stock_client" .build/bin/gosh-server || fail "a stock client with gosh's server"

tools/build-pf7.sh || fail "tools/build-pf7.sh"
pf7=.build/pf7/src/frontend
# Tests ask servers on the Internet nothing: a session that falls back asks only whom it is told to.
MOSH_STUN=none; GOSH_STUN=none; LC_ALL=C.UTF-8; export MOSH_STUN GOSH_STUN LC_ALL
reference() { # NAME ARGS...: one of the fork's tests, against pf.7's programs
    name=$1; shift
    if python3 "tests/$name" "$pf7/mosh-server" "$pf7/mosh-client" "$@" >.build/reference.log 2>&1; then
        echo "pf.7 passes $name $*"
    else
        cat .build/reference.log; stop_ours; fail "pf.7 with tests/$name $*"
    fi
}
reference forward-e2e.py
reference headless-smoke.py
reference headless-smoke.py 127.0.0.1 always
reference headless-smoke.py 127.0.0.1 blocked
reference headless-smoke.py 127.0.0.1 translated
reference headless-smoke.py 127.0.0.1 punched
reference headless-smoke.py 127.0.0.1 returned
# Written here, for requirements the fork tested only from inside (queue item 4).
reference outage.py
reference copy-cut.py
reference copy-liar.py
if python3 -c 'import socket; socket.socket(socket.AF_INET6, socket.SOCK_DGRAM).bind(("::1", 0))' 2>/dev/null; then
    reference headless-smoke.py ::1 blocked
fi
# The two shell tests take the client first, and absolute paths: they change directory.
here=$(pwd)
make -C .build/core/src/tests inpty >/dev/null 2>&1 || fail "building upstream's test helper, inpty"
shell_reference() { # NAME ARGS...
    name=$1; shift
    if "tests/$name" "$here/$pf7/mosh-client" "$here/$pf7/mosh-server" "$@" >.build/reference.log 2>&1; then
        echo "pf.7 passes $name"
    else
        cat .build/reference.log; stop_ours; fail "pf.7 with tests/$name"
    fi
}
shell_reference mocp.test
shell_reference wrapper.test "$here/.build/core/src/tests/inpty"
# The NAT lab, against pf.7. Not "room": it failed at random in the fork's CI, and a
# test that fails at random teaches people to ignore red (queue item 8 settles it).
tools/nat-lab.sh "$pf7" easy hard passed moved || { stop_ours; fail "pf.7 in the NAT lab"; }

# 6. What gosh can do so far (queue item 5): the same tests, against gosh's own programs;
# and a gosh end with a pf.7 end, each way, which holds only while the wire is unchanged.
pair() { # LABEL SERVER CLIENT NAME ARGS...
    label=$1; server=$2; client=$3; name=$4; shift 4
    if python3 "tests/$name" "$server" "$client" "$@" >.build/gosh-test.log 2>&1; then
        echo "$label passes $name $*"
    else
        cat .build/gosh-test.log; stop_ours; fail "$label with tests/$name $*"
    fi
}
gosh=.build/bin
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" forward-e2e.py
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" outage.py
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py
# The relay (REQ1/PATH-1, REQ1/PATH-2), path-finding (REQ1/PATH-3), and the meetup's ticket (REQ15/PATH-4, REQ15/PATH-5).
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 always
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 punched
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" relay-meetup.py
# (the meetup named by another of the machine's addresses: it must answer from that one, REQ17/PATH-8)
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" relay-meetup.py 127.0.0.1 127.0.0.2
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 blocked
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 translated
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 returned
if python3 -c 'import socket; socket.socket(socket.AF_INET6, socket.SOCK_DGRAM).bind(("::1", 0))' 2>/dev/null; then
    pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py ::1 blocked
    # REQ17/PATH-8: path-finding and the meetup by IPv6
    pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py ::1 punched
    pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" relay-meetup.py ::1
fi
# (gosh only: pf.7 fails these two, which are the faults they were written to show. FINDINGS, 2026-11-13 to 15)
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" relay-spoof.py
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" relay-dies.py
# The meetup, as a process (REQ15/PATH-4, REQ15/PATH-5): gosh's alone, since pf.7's has no tickets. (It takes the server alone.)
if python3 tests/meetup-tickets.py "$gosh/gosh-server" >.build/gosh-test.log 2>&1; then
    grep "^SKIPPED" .build/gosh-test.log || true
    echo "$gosh/gosh-server passes meetup-tickets.py"
else
    cat .build/gosh-test.log; stop_ours; fail "$gosh/gosh-server with tests/meetup-tickets.py"
fi
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" copy-cut.py gocp
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" copy-liar.py
if tests/mocp.test "$here/$gosh/gosh-client" "$here/$gosh/gosh-server" gocp gosh >.build/gosh-test.log 2>&1; then
    echo "gosh passes mocp.test"
else
    cat .build/gosh-test.log; stop_ours; fail "gosh with tests/mocp.test"
fi
# (this one takes the command first: gosh-client under the name gosh)
pair "gosh" "$gosh/gosh" "$gosh/gosh-server" gosh-command.py
# (and this one the stock server last: a host with gosh-server, with only mosh-server, with neither)
pair "gosh" "$gosh/gosh" "$gosh/gosh-server" gosh-stock.py "$stock_server"
# The command's own test, from the fork, under gosh's names (REQ1/CMD-1, REQ6/NAME-1)
if tests/wrapper.test "$here/$gosh/gosh-client" "$here/$gosh/gosh-server" "$here/.build/core/src/tests/inpty" gosh gocp \
    >.build/gosh-test.log 2>&1; then
    echo "gosh passes wrapper.test"
else
    cat .build/gosh-test.log; stop_ours; fail "gosh with tests/wrapper.test"
fi
# The installer (REQ1/CMD-2, REQ17/CMD-4), from a release made of these programs, into the test's own home.
if python3 tests/install.py tools/install.sh "$gosh/gosh-client" "$gosh/gosh-server" >.build/gosh-test.log 2>&1; then
    echo "tools/install.sh passes install.py"
else
    cat .build/gosh-test.log; fail "tools/install.sh with tests/install.py"
fi
# A gosh end and a pf.7 end held a forward, a copy and the relay between them until 2026-11-16,
# when the two Instruction fields moved to gosh's own numbers (design v2, question 5; seam
# patches 01 and 02). Since then each takes the other for a stock mosh: the terminal works and
# nothing more is asked (REQ8/CMD-3), which stock-session.py and gosh-stock.py test. pf.7 is
# still run against itself above, as the reference.
echo "PASS"
