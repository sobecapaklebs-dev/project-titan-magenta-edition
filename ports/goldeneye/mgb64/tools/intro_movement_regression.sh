#!/bin/bash
#
# intro_movement_regression.sh -- Post-intro Bond-movement regression lane.
#
# Guards the Silo "look works, WASD dead" regression: after a level intro plays
# to completion, a scripted forward stick must actually move the player. Boots
# each level with the intro enabled (GE007_ENABLE_LEVEL_INTRO=1), holds forward
# (GE007_AUTO_FORWARD) across a post-handoff window, and asserts a minimum
# horizontal displacement via tools/intro_movement_check.py.
#
# Discriminates on the exact axis that broke: Silo is a static-establishing
# intro (the failing class), Dam is a swirl intro (never broke). The lane also
# runs a negative control -- Silo with GE007_NO_POSTINTRO_SPAWN_FIX=1 (the fix's
# A/B opt-out) -- and asserts the player is frozen, proving the detector fires
# and that the fix is load-bearing.
#
# ROM-gated: skips cleanly (exit 0) without a ROM. Captured traces/logs are
# ROM-derived local artifacts -- do not commit them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=240
FORWARD_START=1300
FORWARD_LEN=500
MIN_HORIZONTAL_DELTA="10.0"
# Representative mix of intro types: Silo (static-establishing, the regressor),
# Dam (swirl), Bunker1, Facility, Runway, Frigate.
LEVELS="20 33 9 34 35 26"
ALL_LEVELS="33 34 35 36 9 20 26 43 27 22 24 29 30 25 37 23 39 41 28 32"
OUT_DIR="/tmp/mgb64_intro_movement_regression_$$"

usage() {
    cat <<'USAGE'
Usage: tools/intro_movement_regression.sh [options]

Options:
  --all                    sweep all 20 solo stages
  --level LIST             raw LEVELID list, quoted if multiple
                          (default: "20 33 9 34 35 26")
  --forward-start N        frame the forward stick begins (default: 1300;
                          must be at/after every stage's intro->FP handoff)
  --forward-len N          forward-hold length in frames (default: 500)
  --min-horizontal-delta F minimum post-FP X/Z displacement (default: 10.0)
  --out-dir DIR            output directory (default: /tmp/...)
  --rom PATH               ROM path (default: ./baserom.u.z64)
  --binary PATH            native binary path (default: build/ge007)
  --build-dir DIR          CMake build directory (default: build)
  --no-build               reuse an existing native binary
  --timeout SECONDS        per-capture timeout (default: 240)

Captured traces, logs, and saves are ROM-derived local artifacts. Do not
commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all) LEVELS="$ALL_LEVELS"; shift ;;
        --level) LEVELS="$2"; shift 2 ;;
        --forward-start) FORWARD_START="$2"; shift 2 ;;
        --forward-len) FORWARD_LEN="$2"; shift 2 ;;
        --min-horizontal-delta) MIN_HORIZONTAL_DELTA="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: intro_movement_regression: ROM not found ($ROM); ROM-gated lane."
    exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi
validation_require_binary "$BINARY"

EXIT_FRAME=$((FORWARD_START + FORWARD_LEN + 100))
FORWARD_WINDOW="${FORWARD_START}:${FORWARD_LEN}"

mkdir -p "$OUT_DIR"
trap 'rm -rf "$OUT_DIR"' EXIT

# capture <name> <level> [extra env assignments...]
capture() {
    local name="$1"; shift
    local level="$1"; shift
    local d="$OUT_DIR/$name"
    mkdir -p "$d/save"
    if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
            GE007_ENABLE_LEVEL_INTRO=1 \
            GE007_AUTO_FORWARD="$FORWARD_WINDOW" \
            GE007_AUTO_EXIT_FRAME="$EXIT_FRAME" \
            "$@" \
            "$BINARY" \
            --savedir "$d/save" --rom "$ROM" --level "$level" --deterministic \
            --trace-state "$d/trace.jsonl" >"$d/run.log" 2>&1; then
        echo "FAIL: capture $name (level $level) failed"; tail -20 "$d/run.log" | sed 's/^/  /'
        return 1
    fi
    printf '%s' "$d/trace.jsonl"
}

echo "=== intro_movement_regression (forward $FORWARD_WINDOW, exit $EXIT_FRAME, threshold $MIN_HORIZONTAL_DELTA) ==="

rc=0

for level in $LEVELS; do
    name="lvl_${level}"
    trace="$(capture "$name" "$level")" || { rc=1; continue; }
    if ! python3 tools/intro_movement_check.py "$trace" \
        --label "level-${level}" --forward-start "$FORWARD_START" \
        --min-horizontal-delta "$MIN_HORIZONTAL_DELTA"; then
        rc=1
    fi
done

# Negative control: the fix's A/B opt-out reproduces the frozen-movement bug on
# Silo (static-establishing intro). Proves the detector actually catches a dead
# player and that the spawn-seed fix is what restores movement.
echo "--- negative control: Silo + GE007_NO_POSTINTRO_SPAWN_FIX=1 (must be frozen) ---"
neg_trace="$(capture "negctl_silo" 20 GE007_NO_POSTINTRO_SPAWN_FIX=1)" || neg_trace=""
if [[ -n "$neg_trace" ]]; then
    if ! python3 tools/intro_movement_check.py "$neg_trace" \
        --label "silo-negctl" --forward-start "$FORWARD_START" --expect-frozen; then
        echo "FAIL: negative control did not reproduce the frozen-movement bug"
        rc=1
    fi
else
    rc=1
fi

if [[ "$rc" -ne 0 ]]; then
    echo "FAIL: intro_movement_regression"
    exit 1
fi
echo "PASS: intro_movement_regression"
