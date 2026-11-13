# Sourced by the other tools: where things are, and the build dependencies.
#
# The core needs protobuf and ncurses headers to build. Where the system has
# them, nothing is done. Where it does not and there is no root to install
# them (the machine gosh is developed on), tools/deps.sh unpacks the Debian
# packages under .deps/, and this file points the build at them.

GOSH=$(cd "$(dirname "$0")/.." && pwd)
DEPS=$GOSH/.deps/usr
BUILD=$GOSH/.build

if [ -d "$DEPS/include/google/protobuf" ]; then
    arch=$(gcc -print-multiarch 2>/dev/null || echo x86_64-linux-gnu)
    PATH=$DEPS/bin:$PATH
    LD_LIBRARY_PATH=$DEPS/lib/$arch${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
    export PATH LD_LIBRARY_PATH
    CONFIGURE_DEPS="CPPFLAGS=-isystem\ $DEPS/include LDFLAGS=-L$DEPS/lib/$arch protobuf_CFLAGS=-isystem\ $DEPS/include protobuf_LIBS=-L$DEPS/lib/$arch\ -lprotobuf TINFO_CFLAGS=\  TINFO_LIBS=-ltinfo"
else
    CONFIGURE_DEPS=
fi

# Stops the programs built here and left running: by where the program file is
# (under .build/), never by name. A test that fails can leave its server behind,
# and this machine runs other mosh sessions that are not ours to stop.
stop_ours() {
    for p in /proc/[0-9]*; do
        exe=$(readlink "$p/exe" 2>/dev/null) || continue
        case "$exe" in "$BUILD"/*) kill "${p#/proc/}" 2>/dev/null ;; esac
    done
    return 0
}
