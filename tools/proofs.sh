#!/bin/sh
# The proofs of what stands under src/strong (REQ6/SHAPE-5): what is proved compiled alone, the
# program around it linted, every domain proved, and each of these seen to fail when gosh is broken.
# Success prints "proofs: PASS". tools/check.sh runs it, when the prover's tools are there.
#
#     tools/proofs.sh                     everything: what the whole check is held to
#     tools/proofs.sh --lite [DOMAIN...]  while working: only these domains are proved and broken
#                                         (all of them, proved and not broken, if none is named);
#                                         the lint, which reads all of gosh, is not run
#     tools/proofs.sh --ready             exit 0 if the prover's tools are there; else say what is missing
#
# A domain is a class (or function) proved, and its files stand beside it: D.from (which file, which
# class, what it is written with), D.claims.lean (the proof of what is owed), and, if there are any,
# D.proof.cpp (what is claimed of it, as a test that cannot be run), D.proof-claims.lean (the proof of
# those claims) and D.script.py (more calls for the comparison of model and C++). The prover is
# tools/prover (its README says what each step is), a copy of strong-domain-driven-design's, where
# it is made and tested; what it writes goes to .build/proofs. (PROVER and PROOFS_OUT run another
# prover, writing elsewhere: strong-domain-driven-design's check runs gosh's proofs by its own.)
#
# Needs: Lean 4.36.0 and a Python with libclang, which strong-domain-driven-design owns: found in a
# checkout of it beside gosh (STRONG_DOMAINS; its tools/lean.sh and pixi install make them), else
# set LEAN and PYTHON; in CI, its image has both. And gosh built (tools/build.sh: the lint reads
# mosh's core and its config.h). CONTRIBUTING.md says how.
set -e
cd "$(dirname "$0")/.."
MODE=heavy; ONLY=
for a in "$@"; do
    case "$a" in
        --lite) MODE=lite ;;
        --ready) MODE=ready ;;
        -*) echo "proofs: cannot start: $a is not --lite or --ready"; exit 2 ;;
        *) ONLY="$ONLY $a" ;;
    esac
done
[ "$MODE" = lite ] || [ -z "$ONLY" ] || { echo "proofs: cannot start: a domain is named ($ONLY ), and that is for --lite"; exit 2; }
LEAN_VERSION=4.36.0
: "${STRONG_DOMAINS:=$PWD/../strong-domain-driven-design}"
: "${LEAN:=$STRONG_DOMAINS/.tools/lean-$LEAN_VERSION-linux/bin/lean}"
[ -n "$PYTHON" ] || { [ -x "$STRONG_DOMAINS/.pixi/envs/default/bin/python" ] && PYTHON=$STRONG_DOMAINS/.pixi/envs/default/bin/python; } || PYTHON=python3
export LEAN PYTHON
CORE=$PWD/.build/core
missing=
[ -x "$LEAN" ] || missing="no Lean at $LEAN (set LEAN)"
[ -z "$missing" ] && case "$("$LEAN" --version)" in *"version $LEAN_VERSION,"*) ;; *) missing="$LEAN is not Lean $LEAN_VERSION, the version pinned" ;; esac
[ -n "$missing" ] || "$PYTHON" -c 'import clang.cindex' 2>/dev/null || missing="$PYTHON has no libclang (set PYTHON to one that has)"
[ -n "$missing" ] || [ -f "$CORE/src/include/config.h" ] || missing="gosh has not been built (tools/build.sh): the lint reads mosh's core and its config.h"
if [ "$MODE" = ready ]; then
    [ -z "$missing" ] || { echo "$missing"; exit 1; }
    exit 0
fi
[ -z "$missing" ] || { echo "proofs: cannot start: $missing. CONTRIBUTING.md says how to get the prover's tools"; exit 2; }

# (Not gosh's build CC and CXX, which . tools/gcc16.sh makes g++ 16 with its flags: these are the
# usual compilers, here, in the prover's comparison of model and C++, and where the prover asks CC
# for the compiler's own headers; g++ 16 is run beside them.)
CC=${PROOFS_CC:-cc}; CXX=${PROOFS_CXX:-c++}; export CC CXX

# Each domain, in the order it is proved: one it `uses` before it. Every D.from under src/strong is here.
DOMAINS="StunAnswer bounded_of bounded_checked_of CounterWindow is_stun_request_checked MeetupHeader MeetupNonce MeetupRegister MeetupRegistered MeetupPass MeetupTime"
STRONG=src/strong
FOUND=$(find "$STRONG" -name '*.from' | sed 's|.*/||; s|\.from$||' | sort)
[ "$FOUND" = "$(echo $DOMAINS | tr ' ' '\n' | sort)" ] || { echo "proofs: FAIL: the domains beside the classes ($(echo $FOUND)) are not the ones tools/proofs.sh proves ($DOMAINS)"; exit 1; }

# The prover reads a domain as a directory (from/, claims/, proofs/, scripts/); it is put together
# in .build/proofs from the files beside each class. And it reads gosh's src as it stands in this
# working tree, without the proof files, which are not the program.
W=${PROOFS_OUT:-$PWD/.build/proofs}
rm -rf "$W/from" "$W/claims" "$W/proofs" "$W/scripts" "$W/subject"
mkdir -p "$W/from" "$W/claims" "$W/proofs" "$W/scripts" "$W/subject"
for f in $(find "$STRONG" -name '*.from'); do
    d=$(basename "$f" .from); at=$(dirname "$f")
    cp "$f" "$W/from/$d"
    cp "$at/$d.claims.lean" "$W/claims/${d}Claims.lean"
    [ ! -f "$at/$d.proof.cpp" ] || cp "$at/$d.proof.cpp" "$W/proofs/$d.proof.cpp"
    [ ! -f "$at/$d.proof-claims.lean" ] || cp "$at/$d.proof-claims.lean" "$W/claims/${d}ProofClaims.lean"
    [ ! -f "$at/$d.script.py" ] || cp "$at/$d.script.py" "$W/scripts/$d.py"
done
tar -c --exclude='*.from' --exclude='*.lean' --exclude='*.proof.cpp' --exclude='*.script.py' src | tar -x -C "$W/subject"
SUBJECT=$W/subject
SUBJECT_COMMIT="$(git rev-parse HEAD)$(git diff --quiet HEAD -- src || echo ', and changed since')"
export SUBJECT SUBJECT_COMMIT
PROVER=${PROVER:-$PWD/tools/prover}; export PROVER

# The full compile of what is proved has three steps, in this order: the C++ compiler, the lint,
# the prover. Each refuses the program; the later ones rest on the earlier.

# 1. compile. What is proved compiles alone, with every warning an error: by the usual compiler,
# which reads pre(...) as nothing, and by g++ 16, which compiles it as a contract.
GXX16=$PWD/tools/gcc16/.pixi/envs/default/bin/x86_64-conda-linux-gnu-g++
HEADERS=$(find "$SUBJECT/$STRONG" -name '*.h' | sort)
for f in $HEADERS; do
    "$CXX" -std=gnu++17 -fsyntax-only -Wall -Wextra -Werror -I"$SUBJECT" -x c++ "$f" || { echo "proofs: compile: FAIL: $f ($CXX)"; exit 1; }
    if [ -x "$GXX16" ]; then
        "$GXX16" --sysroot=/ -isystem "/usr/include/$(cc -print-multiarch)" -std=c++26 -fcontracts -fsyntax-only -Wall -Wextra -Werror -I"$SUBJECT" -x c++ "$f" || { echo "proofs: compile: FAIL: $f (g++ 16, with contracts)"; exit 1; }
    fi
done
if [ -x "$GXX16" ]; then echo "compile: ok: $(echo "$HEADERS" | wc -l) files under $STRONG, by $CXX and by g++ 16 with contracts"
else echo "compile: ok: $(echo "$HEADERS" | wc -l) files under $STRONG, by $CXX alone: NOT by g++ 16, so no pre(...) was compiled as a contract (. tools/gcc16.sh fetches it)"; fi

# 2. lint. What a proof takes as given of the program around it, looked for in the program, so
# that a proof has only its own class to answer for. First each class is read by the generator,
# which refuses what it cannot give an exact meaning (a field that is not private, a pointer kept,
# a global): that writes gen/, where the rules find what each class is.
READ_ONLY=1 DOMAIN="$W" "$PROVER/prove.sh" $DOMAINS
# Code that is not proved comes in only by what expects nothing of its caller: it makes no bounded
# number itself and calls nothing that says pre(...). And what is proved stands under src/strong,
# in the namespace Strong, with nothing beside it there that is not proved.
STRONG=$STRONG DOMAIN="$W" "$PYTHON" "$PROVER/ingress.py" $DOMAINS
# And at a call nothing it may write has a second name, nothing starts a thread, and
# nothing outside reaches a proved value's bytes. This reads every file of gosh's src as Clang
# does, so it needs what they include: gosh's vendored headers, and mosh's core with its config.h.
rm -rf "$W/include" && mkdir -p "$W/include" && ln -s "$PWD/vendor" "$W/include/vendor"     # vendor/ alone, not gosh's src a second time
LINT_INCLUDE="$W/include:$CORE:$CORE/src/include"; export LINT_INCLUDE
if [ "$MODE" = lite ]; then
    # --lite: the domains named, and what each is written with (its `uses` lines), in the order DOMAINS has them
    PROVE=$DOMAINS
    if [ -n "$ONLY" ]; then
        want=$ONLY
        for d in $ONLY; do
            [ -f "$W/from/$d" ] || { echo "proofs: cannot start: no domain $d (there are: $DOMAINS)"; exit 2; }
            want="$want $(sed -n 's/^uses //p' "$W/from/$d" | tr '\n' ' ')"
        done
        PROVE=
        for d in $DOMAINS; do case " $want " in *" $d "*) PROVE="$PROVE $d" ;; esac; done
    fi
    DOMAIN="$W" "$PROVER/prove.sh" $PROVE
    if [ -n "$ONLY" ]; then DOMAIN="$W" RED_ONLY="$ONLY" "$PYTHON" tools/proofs-red.py; fi
    echo "proofs: PASS (lite: proved:$PROVE; NOT run: the lint of all of gosh$([ -n "$ONLY" ] && echo ", the other domains' proofs, the rules' red cases" || echo ", any red case"). The whole is tools/proofs.sh)"
    exit 0
fi
STRONG=$STRONG INCLUDE="$LINT_INCLUDE" "$PYTHON" "$PROVER/lint.py"

# 3. prove. Each domain of gosh is proved, from gosh's own files.
DOMAIN="$W" "$PROVER/prove.sh" $DOMAINS
# Each proof fails when its class is broken, and each rule refuses a gosh that breaks it.
DOMAIN="$W" "$PYTHON" tools/proofs-red.py
# (The prover's own tests are strong-domain-driven-design's check: they test the prover, not gosh.)
echo "proofs: PASS"
