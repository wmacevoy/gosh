#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
# Builds the fork at mosh-1.4.0-pf.7 in .build/pf7: the programs gosh is measured
# against (requirements: "the fork ... is what gosh is measured against").
#
# In a directory of gosh's own, and never the installed programs: some of the
# fork's tests stop processes by where their program file is (CONTRIBUTING.md).
# The source is fetched, not kept here: the fork is frozen and is not gosh's.
# PF7_REPO says where from; a path to a local clone works and needs no network.
# What is fetched is checked against the tag's commit, so a moved tag is refused.
set -eu
. "$(dirname "$0")/env.sh"
repo=${PF7_REPO:-https://github.com/wmacevoy/mosh.git}
tag=mosh-1.4.0-pf.7
commit=b00bf061470e096e0e05413a1f1b242aa1b26b31
src=$BUILD/pf7
if [ "$(git -C "$src" rev-parse HEAD 2>/dev/null)" = "$commit" ] && [ -x "$src/src/frontend/mosh-client" ]; then
    echo "build-pf7: already built in $src"; exit 0
fi
chmod -R u+w "$src" 2>/dev/null || true
rm -rf "$src" && mkdir -p "$src"
git -C "$src" init -q
git -C "$src" fetch -q --depth 1 "$repo" "refs/tags/$tag"
got=$(git -C "$src" rev-parse 'FETCH_HEAD^{commit}')
[ "$got" = "$commit" ] || { echo "build-pf7: $tag at $repo is $got, not $commit"; exit 1; }
git -C "$src" checkout -q "$commit"
cd "$src"
./autogen.sh >../pf7.log 2>&1
eval ./configure $CONFIGURE_DEPS >>../pf7.log 2>&1 || { tail -30 ../pf7.log; echo "build-pf7: FAILED at configure (log: $BUILD/pf7.log)"; exit 1; }
make -j"$(nproc 2>/dev/null || echo 2)" >>../pf7.log 2>&1 || { tail -30 ../pf7.log; echo "build-pf7: FAILED (log: $BUILD/pf7.log)"; exit 1; }
echo "build-pf7: $("$src/src/frontend/mosh-client" --version | head -1) in $src"
