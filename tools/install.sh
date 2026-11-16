#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
#
# Installs gosh from a release into your home directory, with no root (REQ1/CMD-2, REQ6/NAME-1):
#
#     curl -fsSL https://github.com/wmacevoy/gosh/releases/latest/download/install.sh | sh
#
# Run it on your own machine and on each machine you log in to. It puts gosh-client, gosh-server,
# gosh and gocp in ~/.local/bin, after checking the download against the release's SHA256SUMS.
# And, each only where no program of that name is on your PATH or already in ~/.local/bin,
# mosh-client and mosh-server as links to gosh's, so that a stock mosh finds a server here
# (REQ6/CMD-4). A program of yours there (a mosh, or another gosh) is never replaced: it says so.
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
while [ "$prefix" != / ] && [ "${prefix%/}" != "$prefix" ]; do prefix=${prefix%/}; done # (no trailing slash)
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
# (and what was staged in $prefix/bin, if it stopped before that was renamed into place)
trap 'rm -rf "$tmp"; rm -f "$prefix/bin/.gosh-client.new" "$prefix/bin/.gosh-server.new"' EXIT INT TERM

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
for f in gosh-client gosh-server; do
    "$tmp/$name/bin/$f" --version >/dev/null 2>&1 \
        || fail "the downloaded $f does not run here; build from source: https://wmacevoy.github.io/gosh/USING.html"
done

# REQ6/CMD-4: a name in $prefix/bin is gosh's to replace only when nothing is there or it is
# gosh's own, a link to gosh-client or gosh-server. Anything else there is the user's: never replaced.
ours() { # NAME
    [ ! -e "$prefix/bin/$1" ] && [ ! -L "$prefix/bin/$1" ] && return 0
    case "$(readlink "$prefix/bin/$1" 2>/dev/null)" in
        gosh-client | gosh-server) return 0 ;;
    esac
    return 1
}
# A program of this name on the PATH that is not one of ours in $prefix/bin?
elsewhere() { # NAME
    found=$(command -v "$1" 2>/dev/null) || return 1
    [ "$found" = "$prefix/bin/$1" ] && ours "$1" && return 1
    return 0
}
link_gosh=yes
link_gocp=yes
link_mosh_client=yes
link_mosh_server=yes
ours gosh || link_gosh=no
ours gocp || link_gocp=no
{ ours mosh-client && ! elsewhere mosh-client; } || link_mosh_client=no
{ ours mosh-server && ! elsewhere mosh-server; } || link_mosh_server=no

mkdir -p "$prefix/bin" "$prefix/share/man/man1"
# Both programs staged before either takes its name, so a copy that fails leaves the old pair as it
# was; then each replaced by rename, never written into: a server may be running from the old one.
for f in gosh-client gosh-server; do
    cp "$tmp/$name/bin/$f" "$prefix/bin/.$f.new"
    chmod 755 "$prefix/bin/.$f.new"
done
for f in gosh-client gosh-server; do
    mv -f "$prefix/bin/.$f.new" "$prefix/bin/$f"
done
link() { # TARGET NAME
    ln -sf "$1" "$prefix/bin/.$2.new"
    mv -f "$prefix/bin/.$2.new" "$prefix/bin/$2"
}
[ "$link_gosh" = yes ] && link gosh-client gosh
[ "$link_gocp" = yes ] && link gosh-client gocp
[ "$link_mosh_client" = yes ] && link gosh-client mosh-client
[ "$link_mosh_server" = yes ] && link gosh-server mosh-server
cp "$tmp/$name"/share/man/man1/*.1 "$prefix/share/man/man1/" 2>/dev/null || true

say "Installed $("$prefix/bin/gosh-client" --version | head -n 1) in $prefix/bin"
left() { # NAME: whose it is that was left
    if ours "$1"; then
        say "($1: yours at $(command -v "$1") is left as it is)"
    else
        say "($1: yours at $prefix/bin/$1 is left as it is)"
    fi
}
[ "$link_mosh_client" = no ] && left mosh-client
[ "$link_mosh_server" = no ] && left mosh-server
[ "$link_gosh" = no ] && say "(gosh: $prefix/bin/gosh is another program, left as it is; run gosh as \"gosh-client gosh ...\")"
[ "$link_gocp" = no ] && say "(gocp: $prefix/bin/gocp is another program, left as it is; run gocp as \"gosh-client gocp ...\")"
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
