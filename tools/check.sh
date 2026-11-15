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

# The unit tests of gosh's parts, built and run inside the build's copy.
. tools/env.sh
for part in copy forwarding relay seam command; do
    if make -C ".build/core/src/$part" check >.build/unit-$part.log 2>&1; then
        echo "unit tests pass: $part ($(grep -c '^PASS:' .build/unit-$part.log))"
    else
        grep -E '^(FAIL|ERROR):|error' .build/unit-$part.log | head -10; fail "unit tests of $part (log: .build/unit-$part.log)"
    fi
done

stock_client=${STOCK_CLIENT:-/usr/bin/mosh-client}
stock_server=${STOCK_SERVER:-/usr/bin/mosh-server}
python3 tests/stock-session.py .build/bin/gosh-client "$stock_server" || fail "gosh's client with a stock server"
python3 tests/stock-session.py "$stock_client" .build/bin/gosh-server || fail "a stock client with gosh's server"

tools/build-pf7.sh || fail "tools/build-pf7.sh"
pf7=.build/pf7/src/frontend
# Tests ask servers on the Internet nothing: a session that falls back asks only whom it is told to.
MOSH_STUN=none; LC_ALL=C.UTF-8; export MOSH_STUN LC_ALL
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
# The relay (REQ1/PATH-1, REQ1/PATH-2), with path-finding off until it lands: so not "punched".
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 always
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 blocked
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 translated
pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 returned
if python3 -c 'import socket; socket.socket(socket.AF_INET6, socket.SOCK_DGRAM).bind(("::1", 0))' 2>/dev/null; then
    pair "gosh" "$gosh/gosh-server" "$gosh/gosh-client" headless-smoke.py ::1 blocked
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
pair "a gosh server and a pf.7 client" "$gosh/gosh-server" "$pf7/mosh-client" forward-e2e.py
pair "a pf.7 server and a gosh client" "$pf7/mosh-server" "$gosh/gosh-client" forward-e2e.py
# (with no relay mode this test ends with a file copied each way over a stream)
pair "a gosh server and a pf.7 client" "$gosh/gosh-server" "$pf7/mosh-client" headless-smoke.py
pair "a pf.7 server and a gosh client" "$pf7/mosh-server" "$gosh/gosh-client" headless-smoke.py
# (the remote relay is the server's program: each of these has one end's relay from each)
pair "a gosh server and a pf.7 client" "$gosh/gosh-server" "$pf7/mosh-client" headless-smoke.py 127.0.0.1 always
pair "a pf.7 server and a gosh client" "$pf7/mosh-server" "$gosh/gosh-client" headless-smoke.py 127.0.0.1 always
echo "PASS"
