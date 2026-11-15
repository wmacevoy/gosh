#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
# Fetches what the core needs to build into .deps/, without root: Debian and
# Ubuntu only (it uses apt-get download). Not needed where these are
# installed: protobuf-compiler libprotobuf-dev libncurses-dev zlib1g-dev.
set -eu
. "$(dirname "$0")/env.sh"
mkdir -p "$GOSH/.deps/debs" && cd "$GOSH/.deps/debs"
apt-get download protobuf-compiler libprotobuf-dev libprotoc32 libncurses-dev zlib1g-dev >/dev/null
for deb in *.deb; do dpkg -x "$deb" "$GOSH/.deps"; done
echo "deps: unpacked $(ls *.deb | wc -l) packages under $GOSH/.deps"
