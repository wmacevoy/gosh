#!/bin/sh
# The full check: the one command a person, an agent and CI all run (SE1/O3).
# Prints PASS and exits 0, or says what failed and exits 1. In order:
#   1. the repository is sound (se --doctor);
#   2. the core is upstream's at the pinned commit, byte for byte (REQ6/UP-2);
#   3. it builds (tools/build.sh);
#   4. gosh and a stock mosh hold a terminal session, each way (REQ10/SHAPE-1).
# A stock mosh 1.4.0 must be installed for 4 (Debian, Ubuntu: the mosh package);
# STOCK_CLIENT and STOCK_SERVER say where, if not in /usr/bin.
cd "$(dirname "$0")/.." || exit 1
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

stock_client=${STOCK_CLIENT:-/usr/bin/mosh-client}
stock_server=${STOCK_SERVER:-/usr/bin/mosh-server}
. tools/env.sh
python3 tests/stock-session.py .build/bin/gosh-client "$stock_server" || fail "gosh's client with a stock server"
python3 tests/stock-session.py "$stock_client" .build/bin/gosh-server || fail "a stock client with gosh's server"
echo "PASS"
