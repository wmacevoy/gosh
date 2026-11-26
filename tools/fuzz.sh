#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
#
# Runs each part's fuzzer under libFuzzer and AddressSanitizer for SECONDS each (queue item 21):
#
#     tools/fuzz.sh [SECONDS]        (60 if not given)
#
# Needs clang, which this machine lacks: CI runs it on every push (.github/workflows/fuzz.yml).
# The check replays every fuzzer's corpus with the ordinary compiler instead (src/seam/fuzz-replay.h).
#
# It builds gosh in .build/ with clang, AddressSanitizer and libFuzzer's coverage, so it replaces
# whatever build is there. Then for each src/PART/.../NAME-fuzzer.cc it builds NAME-fuzzer again
# with libFuzzer's own main (-DGOSH_LIBFUZZER) and runs it on a copy of its corpus. Exit 1 at the
# first fuzzer that crashes: the input is kept as .build/fuzz-PART-NAME-crash-*, and its output shown.
# Crash files of earlier runs are cleared first.
set -eu
. "$(dirname "$0")/env.sh"
seconds=${1:-60}
command -v clang++ >/dev/null 2>&1 || { echo "fuzz: needs clang++ (for libFuzzer); the check replays the corpora without it"; exit 1; }

flags="-g -O1 -fno-omit-frame-pointer -fsanitize=address"
GOSH_CONFIGURE="CC=clang CXX=clang++ CFLAGS='$flags -fsanitize=fuzzer-no-link' CXXFLAGS='$flags -fsanitize=fuzzer-no-link' LDFLAGS=-fsanitize=address" \
    "$GOSH/tools/build.sh"

rm -f "$BUILD"/fuzz-*-crash-* # (so that a crash named below is this run's)
for source in $(cd "$GOSH/src" && find . -name '*-fuzzer.cc' | sort); do
    path=${source#./}       # meetup/imp/registrations-fuzzer.cc, forwarding/tcp/tcpinudp-fuzzer.cc
    part=${path%%/*}
    program=${path#*/}
    program=${program%.cc}  # imp/registrations-fuzzer, tcp/tcpinudp-fuzzer
    dir="$BUILD/core/src/$part"
    rm -f "$dir/$program.o" "$dir/$program"
    make -C "$dir" "$program" CXXFLAGS="$flags -fsanitize=fuzzer -DGOSH_LIBFUZZER" >"$BUILD/fuzz-build.log" 2>&1 \
        || { tail -30 "$BUILD/fuzz-build.log"; echo "fuzz: FAILED to build $part/$program"; exit 1; }
    corpus=$(mktemp -d)
    cp -R "$dir/$program-corpus/." "$corpus/"
    echo "fuzz: $part/$program for $seconds s, from $(ls "$corpus" | wc -l) inputs"
    name=$(basename "$program")
    if ( cd "$dir" && "./$program" -max_total_time="$seconds" -print_final_stats=1 \
            -artifact_prefix="$BUILD/fuzz-$part-$name-" "$corpus" ) >"$BUILD/fuzz-run.log" 2>&1; then
        grep -E "stat::number_of_executed_units|stat::new_units_added" "$BUILD/fuzz-run.log" | sed 's/^/fuzz:   /'
        rm -rf "$corpus"
    else
        tail -40 "$BUILD/fuzz-run.log"
        rm -rf "$corpus"
        echo "fuzz: FAIL $part/$program: the input is in $(ls "$BUILD"/fuzz-"$part"-"$name"-crash-* 2>/dev/null | head -1)"
        exit 1
    fi
done
echo "fuzz: PASS: every fuzzer ran $seconds s with no crash"
