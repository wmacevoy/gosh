#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
# The NAT lab: tests/nat-lab.py, each way asked for, against the programs in a
# directory. Two NATs (Linux's own), a client behind one and a server behind
# the other, in network namespaces of this run's own.
#
# usage: tools/nat-lab.sh BINDIR KIND...     (easy hard passed moved room)
#   BINDIR holds mosh-server and mosh-client. It must be under .build/: the
#   test stops every process whose program file is under BINDIR.
#
# It needs to be root inside a network namespace. Three ways, tried in order:
# already root; an unprivileged user namespace, where the system allows one
# (Debian does); or sudo without a password (a CI runner).
set -eu
. "$(dirname "$0")/env.sh"
bindir=$(cd "$1" && pwd); shift
case "$bindir" in "$BUILD"/*) ;; *) echo "nat-lab: REFUSED: $bindir is not under $BUILD"; exit 1 ;; esac
nft=$(command -v nft || true); [ -n "$nft" ] || nft=/usr/sbin/nft
[ -x "$nft" ] || { echo "nat-lab: NOT RUN: no nft (Debian, Ubuntu: the nftables package)"; exit 1; }
if [ "$(id -u)" = 0 ]; then enter="unshare -m -n"
elif unshare -Urnm --kill-child true 2>/dev/null; then enter="unshare -Urnm --kill-child"
elif sudo -n true 2>/dev/null; then enter="sudo env LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-} unshare -m -n"
else echo "nat-lab: NOT RUN: cannot make a network namespace here (no root, no user namespaces, no sudo)"; exit 1
fi
status=0
for kind in "$@"; do
    if $enter python3 "$GOSH/tests/nat-lab.py" "$bindir" "$nft" "$kind" >"$BUILD/nat-lab-$kind.log" 2>&1; then
        echo "nat-lab: ok $kind ($(grep -c ' ok' "$BUILD/nat-lab-$kind.log") checks)"
    else
        echo "nat-lab: FAIL $kind (log: $BUILD/nat-lab-$kind.log)"
        grep FAIL "$BUILD/nat-lab-$kind.log" | head -5
        status=1
    fi
done
exit $status
