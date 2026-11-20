#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
#
# Collects what tools/build.sh made into a release package (REQ1/CMD-2):
#
#     tools/package-release.sh VERSION OUTPUT_DIRECTORY
#
# Writes OUTPUT_DIRECTORY/gosh-VERSION-linux-ARCH.tar.gz (gosh-VERSION-linux-ARCH-musl.tar.gz on
# musl, as Alpine) or gosh-VERSION-macos-universal.tar.gz,
# laid out as tools/install.sh reads it: bin/ (gosh-client, gosh-server, and gosh and gocp as
# links), share/man/man1/, COPYING, LICENSE-GOSH, LICENSE-MOSH, README.md.
#
# Refuses a program that needs a library the system does not ship (REQ1/SHAPE-3): the point is
# that it is copied to another machine and runs there. Build for it with
#     GOSH_CONFIGURE="--enable-static-libraries --without-utempter" tools/build.sh
# and on musl with GOSH_LDFLAGS=-static as well.
#
# Carried from the fork's scripts/package-release.sh (mosh-1.4.0-pf.7), with gosh's names.

set -eu
. "$(dirname "$0")/env.sh"
version=${1:?usage: package-release.sh VERSION OUTPUT_DIRECTORY}
out=${2:?usage: package-release.sh VERSION OUTPUT_DIRECTORY}
programs="$BUILD/bin/gosh-client $BUILD/bin/gosh-server"

case "$(uname -s)" in
    Linux)
        case "$(uname -m)" in
            x86_64|amd64) arch=x86_64 ;;
            aarch64|arm64) arch=aarch64 ;;
            *) arch=$(uname -m) ;;
        esac
        name="gosh-$version-linux-$arch"
        if [ -e "/lib/ld-musl-$(uname -m).so.1" ]; then
            # musl (Alpine): built with every library linked in, the C library too, so it needs nothing.
            name="$name-musl"
            for program in $programs; do
                if readelf -d "$program" | grep -q NEEDED; then
                    echo "package-release.sh: $program is not built static (GOSH_LDFLAGS=-static):" >&2
                    readelf -d "$program" | grep NEEDED >&2
                    exit 1
                fi
            done
        fi
        for program in $programs; do
            # Only the C library and its companions.
            if ldd "$program" | grep '=>' \
                | grep -v -E 'linux-vdso|libc\.so|libm\.so|ld-linux|libpthread\.so|libdl\.so|librt\.so|libutil\.so' \
                | grep -q .; then
                echo "package-release.sh: $program needs a library the system does not ship:" >&2
                ldd "$program" >&2
                exit 1
            fi
        done
        ;;
    Darwin)
        name="gosh-$version-macos-universal"
        for program in $programs; do
            # Only what ships with macOS.
            if otool -L "$program" | tail -n +2 | grep -v -E '^[[:space:]]*(/usr/lib/|/System/Library/)' \
                | grep -v -E '^[^[:space:]].*:$' | grep -q .; then
                echo "package-release.sh: $program needs a library the system does not ship:" >&2
                otool -L "$program" >&2
                exit 1
            fi
        done
        ;;
    *)
        echo "package-release.sh: no package for $(uname -s)" >&2
        exit 1
        ;;
esac

stage=$(mktemp -d 2>/dev/null || mktemp -d -t gosh-package)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$name/bin" "$stage/$name/share/man/man1" "$out"
for program in gosh-client gosh-server; do
    cp "$BUILD/bin/$program" "$stage/$name/bin/$program"
    if [ "$(uname -s)" = Darwin ]; then
        strip -x "$stage/$name/bin/$program"
        # Apple Silicon runs nothing unsigned, and stripping breaks the linker's signature.
        codesign --force --sign - "$stage/$name/bin/$program"
    else
        strip "$stage/$name/bin/$program"
    fi
done
chmod 755 "$stage/$name/bin/"*
ln -s gosh-client "$stage/$name/bin/gosh"
ln -s gosh-client "$stage/$name/bin/gocp"
for page in "$GOSH"/docs/*.1; do
    [ -f "$page" ] && cp "$page" "$stage/$name/share/man/man1/"
done
cp "$GOSH/COPYING" "$GOSH/LICENSE-GOSH" "$GOSH/LICENSE-MOSH" "$GOSH/README.md" "$stage/$name/"

"$stage/$name/bin/gosh-client" --version | head -n 1
"$stage/$name/bin/gosh-server" --version | head -n 1

tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
echo "$out/$name.tar.gz"
