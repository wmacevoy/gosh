# Sourced by the other tools: where things are, and the build dependencies.
#
# The core needs protobuf and ncurses headers to build. Where the system has
# them, nothing is done. Where it does not and there is no root to install
# them (the machine gosh is developed on), tools/deps.sh unpacks the Debian
# packages under .deps/, and this file points the build at them.

# (tools/check.sh sets GOSH itself: it has changed directory by the time it reads this file,
# and "$0" is then no longer a path from where it stands.)
GOSH=${GOSH:-$(cd "$(dirname "$0")/.." && pwd)}
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

# Stops the programs this run started and left running. A test that fails can leave its
# server behind. "This run's" means both: its program file is under .build/, and it was
# started with this run's mark, GOSH_TEST_RUN, in its environment. Never by name, and not by
# the file alone: this machine runs other mosh sessions, and another run may share .build/.
stop_ours() {
    [ -n "${GOSH_TEST_RUN:-}" ] || return 0
    for p in /proc/[0-9]*; do
        exe=$(readlink "$p/exe" 2>/dev/null) || continue
        case "$exe" in "$BUILD"/*) ;; *) continue ;; esac
        tr '\0' '\n' < "$p/environ" 2>/dev/null | grep -qx "GOSH_TEST_RUN=$GOSH_TEST_RUN" || continue
        kill "${p#/proc/}" 2>/dev/null
    done
    return 0
}
