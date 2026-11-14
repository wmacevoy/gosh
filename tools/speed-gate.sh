#!/bin/sh
# The speed gate (REQ14/SPEED-1): a forward in gosh carries bytes at not more than 10% under
# the fastest version benchmarked so far: a TCP forward by UDP, a copy over a stream, and a
# TCP forward by the relay.
#
# For each way it builds the version tools/fastest names, in a directory of its own under
# .build/, and has tools/speed.py run it turn and turn about with the programs in .build/bin
# (build those first: tools/build.sh). It takes about 25 minutes, so it is not part of the check;
# it is run when a part lands. Exit 0: both ways pass (a way gosh cannot do yet is not counted).
#
# usage: tools/speed-gate.sh [udp|stream|relay]...        (all three, if none is named)
set -eu
cd "$(dirname "$0")/.."
GOSH=$(pwd); export GOSH
. tools/env.sh
[ -x .build/bin/gosh-client ] || { echo "speed-gate: build first (tools/build.sh)"; exit 1; }

# Says where the server and client of a version are, building them if they are not there yet.
programs_of() { # VERSION -> sets server, client
    if [ "$1" = pf.7 ]; then
        tools/build-pf7.sh >&2
        server=$BUILD/pf7/src/frontend/mosh-server; client=$BUILD/pf7/src/frontend/mosh-client
        return
    fi
    tree=$BUILD/fastest-$1
    if [ ! -x "$tree/.build/bin/gosh-client" ]; then
        # A copy of this repository at that commit, with its own core at the commit it pinned,
        # built by its own build script: what was benchmarked, as it was.
        chmod -R u+w "$tree" 2>/dev/null || true
        rm -rf "$tree"
        git clone -q --no-hardlinks . "$tree"
        git -C "$tree" checkout -q "$1"
        git -C "$tree" submodule -q init
        git -C "$tree" config submodule.core.url "$GOSH/core"
        git -C "$tree" -c protocol.file.allow=always submodule -q update
        [ -d .deps ] && ln -s "$GOSH/.deps" "$tree/.deps"
        (cd "$tree" && env -u GOSH -u GOSH_TEST_RUN tools/build.sh >&2) || { echo "speed-gate: could not build $1"; exit 1; }
    fi
    server=$tree/.build/bin/gosh-server; client=$tree/.build/bin/gosh-client
}

status=0
for way in ${@:-udp stream relay}; do
    version=$(awk -v way="$way" '$1 == way { print $2 }' tools/fastest)
    [ -n "$version" ] || { echo "speed-gate: tools/fastest names no version for $way"; exit 1; }
    programs_of "$version"
    echo "speed-gate: $way: against the fastest so far, $version"
    # (the verdict is speed.py's exit status, not that of whatever prints its lines)
    python3 tools/speed.py --way "$way" --against "$server" "$client" .build/bin/gosh-server .build/bin/gosh-client \
        >"$BUILD/speed-gate-$way.log" 2>&1 || status=1
    grep '^speed' "$BUILD/speed-gate-$way.log" || true
done
exit $status
