#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
#
# Installs gosh from a release into your home directory, with no root (REQ1/CMD-2, REQ6/NAME-1):
#
#     curl -fsSL https://github.com/wmacevoy/gosh/releases/latest/download/install.sh | sh
#
# Run it on your own machine and on each machine you log in to. It puts gosh-client, gosh-server,
# gosh and gocp in ~/.local/bin, after checking the download against the release's SHA256SUMS.
# And, each only where no program of that name is found on your PATH, mosh-client and mosh-server
# as links to gosh's, so that a stock mosh finds a server here (REQ6/CMD-4); a mosh you already
# have is never shadowed.
#
# Environment:
#     GOSH_VERSION   the release to install, e.g. 0.2 [the one this script came with, or the newest]
#     PREFIX         where to install [$HOME/.local]
#     GOSH_RELEASES  where releases are downloaded from, for a mirror or a test
#                    [https://github.com/wmacevoy/gosh/releases/download]; a release VERSION is
#                    the directory $GOSH_RELEASES/vVERSION
#
# Carried from the fork's scripts/install.sh (mosh-1.4.0-pf.7), with gosh's names, and the
# links of REQ6/CMD-4 in place of the Perl script.
#
# Fails, with a message and status 1: no curl or wget; a system or machine with no package; a
# download missing, or not as SHA256SUMS says; a client that does not run here.

set -eu

repo="wmacevoy/gosh"
version="${GOSH_VERSION:-@VERSION@}"
prefix="${PREFIX:-$HOME/.local}"
releases="${GOSH_RELEASES:-https://github.com/$repo/releases/download}"

say() { printf '%s\n' "$*"; }
fail() { say "install.sh: $*" >&2; exit 1; }

fetch() { # URL FILE
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$2" "$1"
    else
        fail "need curl or wget"
    fi
}

case "$(uname -s)" in
    Linux) os=linux ;;
    Darwin) os=macos ;;
    *) fail "no package for $(uname -s); build from source: https://wmacevoy.github.io/gosh/USING.html" ;;
esac
case "$(uname -m)" in
    x86_64|amd64) arch=x86_64 ;;
    aarch64|arm64) arch=aarch64 ;;
    *) fail "no package for $(uname -m); build from source: https://wmacevoy.github.io/gosh/USING.html" ;;
esac
[ "$os" = macos ] && arch=universal

tmp=$(mktemp -d 2>/dev/null || mktemp -d -t gosh-install)
trap 'rm -rf "$tmp"' EXIT INT TERM

case "$version" in
    @*)
        # A copy published with a release knows its own version; one run from the source asks.
        fetch "https://api.github.com/repos/$repo/releases?per_page=20" "$tmp/releases" \
            || fail "could not ask https://github.com/$repo/releases for the newest release"
        version=$(sed -n 's/.*"tag_name": *"v\([^"]*\)".*/\1/p' "$tmp/releases" | head -n 1)
        [ -n "$version" ] || fail "found no release at https://github.com/$repo/releases"
        ;;
esac

name="gosh-$version-$os-$arch"
base="$releases/v$version"

say "Downloading $name ..."
fetch "$base/$name.tar.gz" "$tmp/$name.tar.gz" || fail "could not download $base/$name.tar.gz"
fetch "$base/SHA256SUMS" "$tmp/SHA256SUMS" || fail "could not download $base/SHA256SUMS"

want=$(sed -n "s/^\([0-9a-f]\{64\}\) [ *]$name\.tar\.gz\$/\1/p" "$tmp/SHA256SUMS")
[ -n "$want" ] || fail "$name.tar.gz is not listed in SHA256SUMS"
if command -v sha256sum >/dev/null 2>&1; then
    have=$(sha256sum "$tmp/$name.tar.gz" | cut -d' ' -f1)
elif command -v shasum >/dev/null 2>&1; then
    have=$(shasum -a 256 "$tmp/$name.tar.gz" | cut -d' ' -f1)
else
    fail "need sha256sum or shasum to check the download"
fi
[ "$want" = "$have" ] || fail "$name.tar.gz is not what SHA256SUMS says (expected $want, got $have)"

tar -xzf "$tmp/$name.tar.gz" -C "$tmp"
"$tmp/$name/bin/gosh-client" --version >/dev/null 2>&1 \
    || fail "the downloaded gosh-client does not run here; build from source: https://wmacevoy.github.io/gosh/USING.html"

# Is a program of this name found on the PATH, other than a link of ours in $prefix/bin?
elsewhere() {
    found=$(command -v "$1" 2>/dev/null) || return 1
    [ "$found" = "$prefix/bin/$1" ] && [ "$(readlink "$found" 2>/dev/null)" = gosh-client ] && return 1
    [ "$found" = "$prefix/bin/$1" ] && [ "$(readlink "$found" 2>/dev/null)" = gosh-server ] && return 1
    return 0
}
link_mosh_client=yes
link_mosh_server=yes
elsewhere mosh-client && link_mosh_client=no
elsewhere mosh-server && link_mosh_server=no

mkdir -p "$prefix/bin" "$prefix/share/man/man1"
# Replace each file, never write into it: a server may be running from the old one.
for f in gosh-client gosh-server; do
    cp "$tmp/$name/bin/$f" "$prefix/bin/.$f.new"
    chmod 755 "$prefix/bin/.$f.new"
    mv -f "$prefix/bin/.$f.new" "$prefix/bin/$f"
done
link() { # TARGET NAME
    ln -sf "$1" "$prefix/bin/.$2.new"
    mv -f "$prefix/bin/.$2.new" "$prefix/bin/$2"
}
link gosh-client gosh
link gosh-client gocp
[ "$link_mosh_client" = yes ] && link gosh-client mosh-client
[ "$link_mosh_server" = yes ] && link gosh-server mosh-server
cp "$tmp/$name"/share/man/man1/*.1 "$prefix/share/man/man1/" 2>/dev/null || true

say "Installed $("$prefix/bin/gosh-client" --version | head -n 1) as gosh in $prefix/bin"
[ "$link_mosh_client" = no ] && say "(mosh-client: yours at $(command -v mosh-client) is left as it is)"
[ "$link_mosh_server" = no ] && say "(mosh-server: yours at $(command -v mosh-server) is left as it is)"
say "Not independently reviewed: see https://wmacevoy.github.io/gosh/USING.html"

case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *)
        say ""
        say "$prefix/bin is not on your PATH. To run gosh from this machine, add it:"
        say "    export PATH=\"$prefix/bin:\$PATH\""
        say "(As a machine logged in to it needs nothing more: gosh looks in ~/.local/bin there.)"
        ;;
esac
