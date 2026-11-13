#!/bin/sh
# Builds gosh in .build/: a copy of the core at its pinned commit, built there,
# and the two programs under gosh's names in .build/bin (REQ6/NAME-1).
#
# A copy, for two reasons: core/ itself is never written to, so "the core is
# upstream's, byte for byte" (REQ6/UP-2) stays checkable; and it is the copy
# that the seam's patches will be applied to (design, Decisions; queue item 5).
# There are no patches yet: what this builds is upstream's mosh, renamed.
set -eu
. "$(dirname "$0")/env.sh"
[ -f "$GOSH/core/configure.ac" ] || { echo "build: core/ is empty: git submodule update --init"; exit 1; }
chmod -R u+w "$BUILD/core" 2>/dev/null || true
rm -rf "$BUILD/core" "$BUILD/bin" && mkdir -p "$BUILD/core" "$BUILD/bin"
(cd "$GOSH/core" && git ls-files -z | xargs -0 cp --parents -t "$BUILD/core")
cd "$BUILD/core"
./autogen.sh >../build.log 2>&1
eval ./configure $CONFIGURE_DEPS >>../build.log 2>&1 || { tail -30 ../build.log; echo "build: FAILED at configure (log: $BUILD/build.log)"; exit 1; }
make -j"$(nproc 2>/dev/null || echo 2)" >>../build.log 2>&1 || { tail -30 ../build.log; echo "build: FAILED (log: $BUILD/build.log)"; exit 1; }
cp src/frontend/mosh-client "$BUILD/bin/gosh-client"
cp src/frontend/mosh-server "$BUILD/bin/gosh-server"
echo "build: $("$BUILD/bin/gosh-client" --version | head -1) as gosh-client and gosh-server in $BUILD/bin"
