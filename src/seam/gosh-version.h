/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/*
 * Which gosh this is, for --version. tools/build.sh writes this file again in the build's copy with
 * `git describe --tags --always --dirty` of the repository (a release's tag, as v0.2); this text is
 * what a build by any other way says.
 */
#define GOSH_VERSION "unknown (not built by tools/build.sh)"
