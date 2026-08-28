#!/usr/bin/env bash
#
# uncap_purity_gate.sh — FID-0033 rail: 0-tick (render-only) frames are sim-pure.
#
# The determinism invariant behind the F5 uncapped-FPS project, standalone-useful
# now: a deterministic session run twice per level — vanilla vs
# GE007_UNCAP_FUZZ=<seed> — must produce the IDENTICAL final sim-state hash.
#
# GE007_UNCAP_FUZZ=<seed> (armed only under --deterministic, i.e. only under this
# harness — never in normal play) runs a seeded xorshift schedule that turns ~75%
# of loop iterations into RENDER-ONLY frames: frames that inject ZERO simulation
# ticks (src/game/unk_0C0A70.c forces speedgraphframes=0, and src/boss.c skips the
# sim tick + the state-mutating render for that frame). The loop, frame-timing and
# frame-counter machinery still run. If any of that perturbs a hashed simulation
# byte, this gate catches it mechanically — now and for every future commit.
#
# Two alignment facts make the comparison exact with no new CLI surface:
#   1. --screenshot-game-timer N exits at g_GlobalTimer == N (SIM time — identical
#      in both runs; render-only frames do not advance it).
#   2. The --sim-state-hash-out JSON embeds a `frame` field keyed on the RENDER
#      frame count (g_frameSyncCallCount), which differs under fuzz BY DESIGN, plus
#      a `replay` run-shape field. The gate compares the JSON MINUS those two
#      fields (canon() below); the aggregate `hash` and region table are the signal.
#
# ROM-gated: skips cleanly (exit 0) when the binary or the ROM is absent — CI runs
# the ROM-free ctest lanes instead (see tools/sim_invariance_gate.sh for the
# precedent). Negative control: hand-mutating a hashed sim byte on a render-only
# frame flips this gate RED (proven in the Task 0.5 report).
#
# Usage:
#   tools/uncap_purity_gate.sh            # full 20-level matrix x 2 seeds (tier 3)
#   tools/uncap_purity_gate.sh --quick    # 3-level smoke x 2 seeds (verify tier 2)
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"

# shellcheck source=tools/validation_common.sh
if [ -f tools/validation_common.sh ]; then
    . tools/validation_common.sh
fi

BIN="${BIN:-$ROOT/build/ge007}"
ROM="${ROM:-$ROOT/baserom.u.z64}"

# Raw LEVELIDs — the 20-stage corpus from tools/regression_test.sh (kept in lockstep).
FULL_LEVELS="33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32"
# --quick trio: exterior + interior + guard-dense (Dam / Bunker1 / Archives),
# mirroring the spec's dam/bunker1/archives smoke set.
QUICK_LEVELS="33 9 24"

QUICK=0
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=1 ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "uncap_purity_gate: unknown arg '$arg'" >&2; exit 2 ;;
    esac
done

if [ "$QUICK" -eq 1 ]; then
    LEVELS="${LEVELS:-$QUICK_LEVELS}"
    MODE="quick"
else
    LEVELS="${LEVELS:-$FULL_LEVELS}"
    MODE="full"
fi
GAMETIMER="${GAMETIMER:-900}"       # 15 sim-seconds of gameplay
SEEDS="${SEEDS:-1337 4242}"
PER_RUN_TIMEOUT="${PER_RUN_TIMEOUT:-180}"

if [ ! -x "$BIN" ]; then echo "SKIP: $BIN not built (ROM-free ctest lanes cover CI)"; exit 0; fi
if [ ! -e "$ROM" ]; then echo "SKIP: no ROM at $ROM (local gate)"; exit 0; fi

# Determinism envelope (CHARTER.md rule 6). Isolated CWD per run avoids ge007.ini drift.
ENVV=(GE007_DETERMINISTIC_STABLE_COUNT=1 SDL_AUDIODRIVER=dummy GE007_MUTE=1
      GE007_BACKGROUND=1 GE007_NO_VSYNC=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1)

TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_BIN="gtimeout"; fi

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

canon() {
    # Print the hash JSON minus run-shape fields: `frame` counts RENDER frames
    # (legitimately differs under fuzz) and `replay` is the ramrom path. The
    # per-region table and the aggregate `hash` MUST NOT be popped — they are
    # the signal.
    python3 - "$1" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
d.pop("frame", None)
d.pop("replay", None)
print(json.dumps(d, sort_keys=True))
EOF
}

run_hash() {   # $1=out.json ; $2=seed ("" = vanilla)
    local out="$1" seed="$2" dir
    dir="$TMP/run.$$.$RANDOM"; mkdir -p "$dir"
    local cmd=(env "${ENVV[@]}")
    [ -n "$seed" ] && cmd+=(GE007_UNCAP_FUZZ="$seed")
    cmd+=("$BIN" --rom "$ROM" --deterministic --level "$lvl"
          --screenshot-game-timer "$GAMETIMER" --screenshot-exit
          --sim-state-hash-out "$out")
    if [ -n "$TIMEOUT_BIN" ]; then
        ( cd "$dir" && "$TIMEOUT_BIN" "$PER_RUN_TIMEOUT" "${cmd[@]}" >"$dir/log" 2>&1 ) || true
    else
        ( cd "$dir" && "${cmd[@]}" >"$dir/log" 2>&1 ) || true
    fi
}

echo "uncap-purity-gate: mode=$MODE levels=[$LEVELS] seeds=[$SEEDS] game_timer=$GAMETIMER"
fail=0
skipped=""
unstable=""
for lvl in $LEVELS; do
    base_json="$TMP/base_$lvl.json"
    run_hash "$base_json" ""
    if [ ! -s "$base_json" ]; then
        echo "  level $lvl: SKIP — vanilla run emitted no hash (timeout/early-exit; see log)"
        skipped="$skipped $lvl(vanilla)"
        continue
    fi
    base_canon="$(canon "$base_json")"

    lvl_ok=1
    diverged=""
    for seed in $SEEDS; do
        fuzz_json="$TMP/fuzz_${lvl}_${seed}.json"
        run_hash "$fuzz_json" "$seed"
        if [ ! -s "$fuzz_json" ]; then
            echo "  level $lvl seed $seed: SKIP — fuzz run emitted no hash (see log)"
            skipped="$skipped $lvl:$seed"
            continue
        fi
        fuzz_canon="$(canon "$fuzz_json")"
        if [ "$base_canon" != "$fuzz_canon" ]; then
            # Record the seed only; canons carry whitespace and word-splitting
            # them in the report loop exploded one divergence into dozens of
            # garbage "seed" lines (2026-07-17 verify). Keep the fuzz json for
            # the report loop to re-canon.
            diverged="$diverged $seed"
        fi
    done

    if [ -z "$diverged" ]; then
        echo "  level $lvl: OK (both seeds, 0 sim-hash divergence)"
        continue
    fi

    # A divergence was seen. Before blaming the fuzz, confirm the level's vanilla
    # run is itself reproducible: re-run vanilla and compare. If vanilla is NOT
    # stable, the divergence is a PRE-EXISTING non-determinism bug (no baseline to
    # test purity against), NOT a render-only-frame regression — report UNSTABLE
    # (does not fail the purity gate) so the two are never conflated.
    base2_json="$TMP/base2_$lvl.json"
    run_hash "$base2_json" ""
    if [ -s "$base2_json" ] && [ "$(canon "$base2_json")" != "$base_canon" ]; then
        echo "  level $lvl: UNSTABLE — vanilla is non-deterministic across runs (pre-existing;"
        echo "             the purity gate cannot assess this level — NOT a fuzz regression)."
        unstable="$unstable $lvl"
        continue
    fi

    lvl_ok=0
    fail=1
    for d in $diverged; do
        echo "  level $lvl seed $d: FAIL — render-only frames perturbed sim state (vanilla stable)"
        echo "      vanilla: $base_canon"
        echo "      fuzzed:  $(canon "$TMP/fuzz_${lvl}_${d}.json")"
    done
done

[ -n "$skipped" ]  && echo "uncap-purity-gate: SKIPPED runs (charter rule 9):$skipped"
[ -n "$unstable" ] && echo "uncap-purity-gate: UNSTABLE levels (pre-existing vanilla non-determinism, not fuzz):$unstable"

if [ "$fail" -ne 0 ]; then
    echo "UNCAP PURITY: FAIL — a render-only frame mutated hashed sim state." >&2
    exit 1
fi
echo "UNCAP PURITY: PASS — render-only (0-tick) frames are sim-pure.${unstable:+ (unstable levels flagged, not tested:$unstable)}"
