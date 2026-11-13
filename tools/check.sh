#!/bin/sh
# The full check: the one command a person, an agent and CI all run (SE1/O3).
# Today it can only ask whether the repository is sound, because there is no
# code to build yet; queue item 3 adds the build and the tests to this file.
# Prints PASS and exits 0, or says what failed and exits 1.
cd "$(dirname "$0")/.." || exit 1

# The doctor asks the network whether its guide is current. On a machine that has
# never been able to ask (a fresh CI runner, with --offline) it fails rather than
# say "current" unasked, so it is left to ask. So on such a machine this check
# needs api.github.com to answer; when it does not, the line is
# "[fail] release check ... not made", and the remedy is to run it again.
if ! python3 se.py --doctor; then
  echo "FAIL: se --doctor"
  exit 1
fi
echo "PASS"
