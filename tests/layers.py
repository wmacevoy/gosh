#!/usr/bin/env python3
# Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH.
"""REQ6/SHAPE-5: gosh's parts keep to their layers (src/README.md). SHAPE-5 is judged: this can
only veto it, never pass it.

usage: layers.py [SRC]

Reads every #include "src/PART/..." in gosh's own parts (SRC, default src/ beside this file's
directory) and fails on one that crosses layers. The layers:

  glue         command, seam        may include any part
  application  copy                 its own layer only
  carrying     forwarding           its own layer only
  paths        relay, pathfinding,  its own layer only
               meetup

Any part may include the core's directories, and two of the seam's files that are tooling, not
glue: fuzz-replay.h (the fuzzers' replay main) and gosh-version.h (written by the build). A part
not in the table fails too: a new part's layer is a decision, to be written here and in
src/README.md.

Written 2026-11-17 and seen red with forwarding including copy, as it did until that day.
"""
import os
import re
import sys

LAYERS = {
    "command": "glue", "seam": "glue",
    "copy": "application",
    "forwarding": "carrying",
    "relay": "paths", "pathfinding": "paths", "meetup": "paths",
}
TOOLING = {"seam/fuzz-replay.h", "seam/gosh-version.h"}

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
include = re.compile(r'^\s*#\s*include\s+"src/([^/"]+)/([^"]+)"')
failures = []
for part in sorted(os.listdir(src)):
    if not os.path.isdir(os.path.join(src, part)):
        continue
    if part not in LAYERS:
        failures.append("%s: a part in no layer (add it to tests/layers.py and src/README.md)" % part)
        continue
    for root, _, files in os.walk(os.path.join(src, part)):
        for name in sorted(files):
            if not name.endswith((".cc", ".h")):
                continue
            path = os.path.join(root, name)
            with open(path, errors="replace") as f:
                for number, line in enumerate(f, 1):
                    m = include.match(line)
                    if not m:
                        continue
                    other = m.group(1)
                    if other not in LAYERS or "%s/%s" % (other, m.group(2)) in TOOLING:
                        continue  # the core's, or tooling
                    if LAYERS[part] != "glue" and LAYERS[other] != LAYERS[part]:
                        failures.append("%s:%d: %s (%s) includes %s (%s)" % (
                            os.path.relpath(path, os.path.join(src, "..")), number, part, LAYERS[part], other, LAYERS[other]))

for f in failures:
    print("FAIL " + f)
if failures:
    sys.exit("FAIL REQ6/SHAPE-5: %d crossings of the layers" % len(failures))
print("PASS (a veto not used; REQ6/SHAPE-5 is judged) every part keeps to its layer; only glue reaches across")
