#!/usr/bin/env bash
#
# intro_parse_digest_gate.sh -- T3 emulator-free intro-stream parse digest
# determinism gate (LOCAL preflight).
#
# Runs tools/intro_parse_digest.py across the 20 solo-campaign stages TWICE,
# into two separate temp out-dirs, and diffs the per-stage SHA-256 digests.
# A mismatch means the N64->PC intro-record widening loop (src/game/prop.c,
# ~line 2972) produced a NONDETERMINISTIC parse of the SAME setup data across
# two runs -- a loader/walker regression, caught without an N64 emulator
# oracle or even a rendered frame.
#
# Optionally also diffs sweep 1 against a LOCAL baseline dir
# (MGB64_INTRO_DIGEST_BASELINE env or --baseline DIR) of previously captured
# intro_digest_<stage>.json files -- e.g. saved before a prop.c/bondview.c
# change -- to catch a regression against a known-good parse. A missing
# baseline (dir absent, or absent for a given stage) is NOT a failure, just
# a note: this lets the gate run standalone with no setup required.
#
# Requires a ROM + built binary, so this is a LOCAL gate. Missing ROM/binary
# follows the SAME convention as tools/sim_invariance_gate.sh and
# tools/ssao_gate.sh in this repo: print a message and exit 2 (NOT a soft
# SKIP+exit 0 -- checked both of those scripts before writing this one).
#
# Usage: tools/intro_parse_digest_gate.sh [--baseline DIR] [--stages "LIST"]
#                                          [--binary PATH] [--rom PATH]
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"

BIN="${GE007_BIN:-$ROOT/build/ge007}"
ROM="${GE007_ROM:-$ROOT/baserom.u.z64}"
STAGES="${GE007_INTRO_DIGEST_STAGES:-33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32}"
BASELINE="${MGB64_INTRO_DIGEST_BASELINE:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --baseline) BASELINE="$2"; shift 2 ;;
        --stages) STAGES="$2"; shift 2 ;;
        --binary) BIN="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,24p' "$0" >&2
            exit 0
            ;;
        *) echo "intro-parse-digest-gate: unknown arg '$1'" >&2; exit 2 ;;
    esac
done

[ -x "$BIN" ] || { echo "intro-parse-digest-gate: $BIN not built" >&2; exit 2; }
[ -e "$ROM" ] || { echo "intro-parse-digest-gate: ROM $ROM not found (local gate)" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "intro-parse-digest-gate: python3 not found" >&2; exit 2; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
RUN1="$TMP/run1"
RUN2="$TMP/run2"

sha_of() {  # $1 = intro_digest_<stage>.json path
    python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['sha256'])" "$1"
}

echo "intro-parse-digest-gate: binary=$BIN"
echo "  rom:    $ROM"
echo "  stages: $STAGES"

echo ""
echo "=== sweep 1/2 ==="
if ! python3 "$ROOT/tools/intro_parse_digest.py" \
        --binary "$BIN" --rom "$ROM" --stages "$STAGES" --out-dir "$RUN1"; then
    echo "intro-parse-digest-gate: FAIL -- sweep 1 capture failed" >&2
    exit 1
fi

echo ""
echo "=== sweep 2/2 ==="
if ! python3 "$ROOT/tools/intro_parse_digest.py" \
        --binary "$BIN" --rom "$ROM" --stages "$STAGES" --out-dir "$RUN2"; then
    echo "intro-parse-digest-gate: FAIL -- sweep 2 capture failed" >&2
    exit 1
fi

echo ""
echo "=== determinism check (sweep 1 vs sweep 2) ==="
fail=0
for stage in $STAGES; do
    f1="$RUN1/intro_digest_${stage}.json"
    f2="$RUN2/intro_digest_${stage}.json"
    sha1="$(sha_of "$f1")"
    sha2="$(sha_of "$f2")"
    if [ "$sha1" != "$sha2" ]; then
        echo "  stage $stage: MISMATCH sweep1=$sha1 sweep2=$sha2" >&2
        fail=1
    else
        echo "  stage $stage: $sha1"
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "intro-parse-digest-gate: FAIL -- nondeterministic parse (see mismatches above)" >&2
    exit 1
fi
# shellcheck disable=SC2086 # intentional word-splitting of a space-separated list
n_stages=$(printf '%s\n' $STAGES | wc -w | tr -d ' ')
echo "intro-parse-digest-gate: PASS -- all ${n_stages} stage digests stable across two sweeps"

if [ -z "$BASELINE" ]; then
    echo ""
    echo "intro-parse-digest-gate: NOTE -- no baseline set (MGB64_INTRO_DIGEST_BASELINE or --baseline); skipping baseline comparison"
    exit 0
fi

if [ ! -d "$BASELINE" ]; then
    echo ""
    echo "intro-parse-digest-gate: NOTE -- baseline dir not found: $BASELINE (skipping baseline comparison)"
    exit 0
fi

echo ""
echo "=== baseline comparison ($BASELINE) ==="
baseline_fail=0
baseline_missing=0
for stage in $STAGES; do
    bfile="$BASELINE/intro_digest_${stage}.json"
    if [ ! -f "$bfile" ]; then
        echo "  stage $stage: NOTE -- no baseline digest at $bfile"
        baseline_missing=1
        continue
    fi
    bsha="$(sha_of "$bfile")"
    sha1="$(sha_of "$RUN1/intro_digest_${stage}.json")"
    if [ "$bsha" != "$sha1" ]; then
        echo "  stage $stage: MISMATCH vs baseline baseline=$bsha current=$sha1" >&2
        baseline_fail=1
    else
        echo "  stage $stage: matches baseline ($bsha)"
    fi
done

if [ "$baseline_fail" -ne 0 ]; then
    echo "intro-parse-digest-gate: FAIL -- current parse diverges from local baseline" >&2
    exit 1
fi
if [ "$baseline_missing" -ne 0 ]; then
    echo "intro-parse-digest-gate: NOTE -- baseline comparison incomplete (some stages had no baseline digest)"
fi
echo "intro-parse-digest-gate: PASS -- matches local baseline"
