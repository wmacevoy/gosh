#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
# Builds and checks gosh with g++ 16, as C++26 with contracts (pre, post, contract_assert) on:
#
#     . tools/gcc16.sh        then tools/build.sh, tools/check.sh as usual
#
# It sets CC and CXX, which the core's configure takes. The compiler is conda-forge's, put in
# tools/gcc16/.pixi by pixi (https://pixi.sh), with no root; tools/gcc16/pixi.lock says exactly
# which. Everything else is the system's: its C library, protobuf, ncurses, OpenSSL. So the
# compiler is told to look where the system keeps them (--sysroot=/ and the multiarch
# directories), and the programs are told where this compiler's own libstdc++ is (-rpath).
#
# A violated contract ends the program and says which (the "enforce" semantic, g++'s default).
# GOSH_CONTRACTS=ignore builds them to nothing; observe and quick_enforce are g++'s other two.
#
# Tried on Debian 12, x86_64, alone. Not on arm64, macOS or Windows; and nothing in CI uses it.
here=$(cd "$(dirname "${BASH_SOURCE:-$0}")" 2>/dev/null && pwd) || here=$PWD/tools
command -v pixi >/dev/null 2>&1 || { echo "gcc16: no pixi (https://pixi.sh): it fetches the compiler"; return 1 2>/dev/null || exit 1; }
(cd "$here/gcc16" && pixi install --locked >/dev/null) || { echo "gcc16: pixi could not install the compiler"; return 1 2>/dev/null || exit 1; }
gcc16=$here/gcc16/.pixi/envs/default
multiarch=$(cc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)
system="--sysroot=/ -isystem /usr/include/$multiarch -B/usr/lib/$multiarch -L/usr/lib/$multiarch -Wl,-rpath,$gcc16/lib"
CC="$gcc16/bin/x86_64-conda-linux-gnu-gcc $system"
CXX="$gcc16/bin/x86_64-conda-linux-gnu-g++ $system -std=c++26 -fcontracts -fcontract-evaluation-semantic=${GOSH_CONTRACTS:-enforce}"
export CC CXX
echo "gcc16: $("$gcc16/bin/x86_64-conda-linux-gnu-g++" --version | head -1), C++26, contracts: ${GOSH_CONTRACTS:-enforce}"
