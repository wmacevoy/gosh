#!/bin/sh
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
# The one thing gosh's development needs that needs root on this machine: a
# profiler, for the profile of pf.7 that queue item 4 asks for (design v2,
# question 7). Run it once, as root:
#
#     sudo tools/setup-root.sh
#
# It does two things and says what it did:
#   1. installs Debian's linux-perf package (the "perf" program);
#   2. sets kernel.perf_event_paranoid to 2, until the next restart.
#
# About 2: Debian ships 3, which lets nobody but root measure anything. 2 is the
# kernel's own default: a user may profile their own processes, in user space
# only, and nothing of other users' or of the kernel. Pass --keep to write it to
# /etc/sysctl.d as well, so that it survives a restart; without that, a restart
# puts 3 back.
#
# Nothing else here needs root. The NAT lab runs in an unprivileged user
# namespace, which this machine allows, and nft is already installed. The build
# needs no root either (tools/deps.sh).
set -eu
[ "$(id -u)" = 0 ] || { echo "setup-root: run as root: sudo $0"; exit 1; }

if command -v perf >/dev/null 2>&1; then
    echo "setup-root: perf is already installed: $(perf --version)"
else
    apt-get install -y linux-perf
    echo "setup-root: installed $(perf --version)"
fi

was=$(cat /proc/sys/kernel/perf_event_paranoid)
if [ "$was" -gt 2 ]; then
    sysctl -q -w kernel.perf_event_paranoid=2
    echo "setup-root: kernel.perf_event_paranoid was $was, is now 2 (until restart)"
else
    echo "setup-root: kernel.perf_event_paranoid is $was already; left alone"
fi

if [ "${1:-}" = --keep ]; then
    echo "kernel.perf_event_paranoid = 2" > /etc/sysctl.d/90-gosh-perf.conf
    echo "setup-root: wrote /etc/sysctl.d/90-gosh-perf.conf, so it survives a restart (delete the file to undo)"
fi
