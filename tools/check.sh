#!/bin/sh
# The full check: the one command a person, an agent and CI all run (SE1/O3).
# Today it can only ask whether the repository is sound, because there is no
# code to build yet; queue item 3 adds the build and the tests to this file.
# Prints PASS and exits 0, or says what failed and exits 1.
cd "$(dirname "$0")/.." || exit 1

# --offline: the check must not depend on reaching the guide's release page.
if ! python3 se.py --doctor --offline; then
  echo "FAIL: se --doctor"
  exit 1
fi
echo "PASS"
