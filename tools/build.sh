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
# series builds and works by itself. GOSH_CONFIGURE adds words to configure's, as a
# release does: "--enable-static-libraries --without-utempter" (tools/package-release.sh).
set -eu
. "$(dirname "$0")/env.sh"
[ -f "$GOSH/core/configure.ac" ] || { echo "build: core/ is empty: git submodule update --init"; exit 1; }
chmod -R u+w "$BUILD/core" 2>/dev/null || true
rm -rf "$BUILD/core" "$BUILD/bin" && mkdir -p "$BUILD/core" "$BUILD/bin"
(cd "$GOSH/core" && git ls-files -z | tar --null -T - -cf - | tar -xf - -C "$BUILD/core")
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
    cp -R "${part%/}" "$BUILD/core/src/" # (no trailing slash: BSD cp would copy only what is inside)
done
# The testing library the parts' tests use (vendor/facts/README.md), where they include it from.
mkdir -p "$BUILD/core/vendor"
cp -R "$GOSH/vendor/facts" "$BUILD/core/vendor/"
# retain/recall (vendor/retain/README.md): how the seam's hooks find the session gosh's main made.
cp -R "$GOSH/vendor/retain" "$BUILD/core/vendor/"
# Which gosh this is, for --version (src/seam/gosh-version.h): a release's tag, or the commit.
gosh_version=$(git -C "$GOSH" describe --tags --always --dirty 2>/dev/null || echo unknown)
printf '#define GOSH_VERSION "%s"\n' "$gosh_version" > "$BUILD/core/src/seam/gosh-version.h"
./autogen.sh >../build.log 2>&1 || { tail -30 ../build.log; echo "build: FAILED at autogen (log: $BUILD/build.log)"; exit 1; }
eval ./configure $CONFIGURE_DEPS ${GOSH_CONFIGURE:-} >>../build.log 2>&1 || { tail -30 ../build.log; echo "build: FAILED at configure (log: $BUILD/build.log)"; exit 1; }
# src/ alone, and the version stamp it reads: the core's scripts/ is upstream's Perl mosh, which gosh
# does not ship (REQ1/CMD-1) and whose check needs Perl's diagnostics; man/ and conf/ are not shipped either.
{ make VERSION.stamp && make -C src -j"$(nproc 2>/dev/null || echo 2)"; } >>../build.log 2>&1 || { tail -30 ../build.log; echo "build: FAILED (log: $BUILD/build.log)"; exit 1; }
cp src/frontend/mosh-client "$BUILD/bin/gosh-client"
cp src/frontend/mosh-server "$BUILD/bin/gosh-server"
# gosh and gocp are gosh-client under those names (REQ6/NAME-1; src/seam/client-main.cc).
ln -sf gosh-client "$BUILD/bin/gosh"
ln -sf gosh-client "$BUILD/bin/gocp"
echo "build: $("$BUILD/bin/gosh-client" --version | head -1) as gosh-client and gosh-server in $BUILD/bin"
