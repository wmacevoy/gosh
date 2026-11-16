#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
#
# Replays every part's fuzzer (src/PART/.../NAME-fuzzer.cc) under AddressSanitizer, with the
# ordinary compiler (queue item 21; its review, 2026-11-16): the replay in `make check` runs
# without it, so a read past the end of a buffer that happens not to crash passed the check.
#
#     tools/asan-replay.sh
#
# It builds gosh again in .build/asan/ (the ordinary build in .build/ is left as it is) with
# -fsanitize=address, then builds and runs each fuzzer there: its corpus and the replay's seeded
# changes of it (src/seam/fuzz-replay.h). Exit 1 at the first that fails: its output is shown, and
# the input is kept beside the program as NAME-fuzzer-crash.
#
# Leaks are not looked for (detect_leaks=0): the fuzzers hold state for their whole run on purpose.
set -eu
GOSH=$(cd "$(dirname "$0")/.." && pwd)
GOSH_BUILD_DIR="$GOSH/.build/asan"
export GOSH GOSH_BUILD_DIR
flags="-g -O1 -fno-omit-frame-pointer -fsanitize=address"
GOSH_LDFLAGS=-fsanitize=address GOSH_CONFIGURE="CFLAGS='$flags' CXXFLAGS='$flags'" "$GOSH/tools/build.sh" >/dev/null \
    || { echo "asan-replay: the build with AddressSanitizer failed (log: $GOSH_BUILD_DIR/build.log)"; exit 1; }
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1
export ASAN_OPTIONS

count=0
for source in $(cd "$GOSH/src" && find . -name '*-fuzzer.cc' | sort); do
    path=${source#./}       # meetup/registrations-fuzzer.cc, forwarding/tcp/tcpinudp-fuzzer.cc
    part=${path%%/*}
    program=${path#*/}
    program=${program%.cc}  # registrations-fuzzer, tcp/tcpinudp-fuzzer
    dir="$GOSH_BUILD_DIR/core/src/$part"
    make -C "$dir" "$program" >"$GOSH_BUILD_DIR/asan-build.log" 2>&1 \
        || { tail -20 "$GOSH_BUILD_DIR/asan-build.log"; echo "asan-replay: FAILED to build $part/$program"; exit 1; }
    if ( cd "$dir" && "./$program" ) >"$GOSH_BUILD_DIR/asan-run.log" 2>&1; then
        count=$((count + 1))
    else
        tail -40 "$GOSH_BUILD_DIR/asan-run.log"
        echo "asan-replay: FAIL $part/$program under AddressSanitizer"
        exit 1
    fi
done
[ "$count" -gt 0 ] || { echo "asan-replay: found no fuzzer"; exit 1; }
echo "asan-replay: $count fuzzers replayed under AddressSanitizer"
