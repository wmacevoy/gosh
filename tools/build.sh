#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
# Builds gosh in .build/: a copy of the core at its pinned commit, built there,
# and the two programs under gosh's names in .build/bin (REQ6/NAME-1).
#
# A copy, for two reasons: core/ itself is never written to, so "the core is
# upstream's, byte for byte" (REQ6/UP-2) stays checkable; and it is the copy
# that the seam's patches are applied to, in the order of their names
# (REQ10/SHAPE-1; seam/README.md). A patch that does not apply exactly stops the
# build: that is how a moved pin says what the seam costs.
#
# SEAM_UPTO=N applies only the first N patches, to check that each stage of the
# series builds and works by itself.
set -eu
. "$(dirname "$0")/env.sh"
[ -f "$GOSH/core/configure.ac" ] || { echo "build: core/ is empty: git submodule update --init"; exit 1; }
chmod -R u+w "$BUILD/core" 2>/dev/null || true
rm -rf "$BUILD/core" "$BUILD/bin" && mkdir -p "$BUILD/core" "$BUILD/bin"
(cd "$GOSH/core" && git ls-files -z | xargs -0 cp --parents -t "$BUILD/core")
cd "$BUILD/core"
applied=0
for patch in "$GOSH"/seam/*.patch; do
    [ -f "$patch" ] || continue
    [ "$applied" -lt "${SEAM_UPTO:-999}" ] || break
    # --fuzz=0: the lines around each change must be exactly the core's, or it is refused.
    patch -p1 --fuzz=0 --no-backup-if-mismatch -s < "$patch" \
        || { echo "build: the seam's $(basename "$patch") does not apply to the core"; exit 1; }
    applied=$((applied + 1))
done
echo "build: $applied of the seam's patches applied"
# gosh's own parts go into the copy beside the core's directories, where the core's
# build (told by the seam's build patch) and its include paths find them.
for part in "$GOSH"/src/*/; do
    [ -d "$part" ] || continue
    cp -r "$part" "$BUILD/core/src/"
done
./autogen.sh >../build.log 2>&1
eval ./configure $CONFIGURE_DEPS >>../build.log 2>&1 || { tail -30 ../build.log; echo "build: FAILED at configure (log: $BUILD/build.log)"; exit 1; }
make -j"$(nproc 2>/dev/null || echo 2)" >>../build.log 2>&1 || { tail -30 ../build.log; echo "build: FAILED (log: $BUILD/build.log)"; exit 1; }
cp src/frontend/mosh-client "$BUILD/bin/gosh-client"
cp src/frontend/mosh-server "$BUILD/bin/gosh-server"
# gosh and gocp are gosh-client under those names (REQ6/NAME-1; src/seam/client-main.cc).
ln -sf gosh-client "$BUILD/bin/gosh"
ln -sf gosh-client "$BUILD/bin/gocp"
echo "build: $("$BUILD/bin/gosh-client" --version | head -1) as gosh-client and gosh-server in $BUILD/bin"
