#!/bin/bash
#
# intro_visual_regression.sh -- Pixel-level Bond level-intro validator (M1.4/R8).
#
# Proves the animated Dam intro renders an intact, grounded, shard-free Bond, and
# that the validator actually catches the broken states -- not just the happy
# path. Captures the intro at a deterministic screenshot frame under several env
# configs and asserts each one's expected verdict from tools/analyze_intro_body.py:
#
#   normal                    -> PASS (Bond present, grounded, no shards)
#   GE007_NO_BOND_BODY_FIX=1   -> FAIL presence   (viewer body aliased/absent)
#   GE007_INTRO_BODY_Y_OFFSET  -> FAIL grounding   (deliberately floated root)
#   GE007_NO_INTRO_PHASE3=1    -> PASS (older-but-not-broken: static phase-2 idle)
#   GE007_NO_INTRO_ROOTMOTION=1-> PASS (older-but-not-broken: pinned spawn anchor)
#   shard self-test            -> FAIL shards  (dark-red pixels injected into the
#                                               normal frame; proves the detector)
#
# ROM-gated: skips cleanly (exit 0) without a ROM. Captured screenshots/traces are
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
FRAME=900
LEVEL=33
OUT_DIR="/tmp/mgb64_intro_visual_regression_$$"

usage() {
    cat <<'USAGE'
Usage: tools/intro_visual_regression.sh [options]

Options:
  --out-dir DIR      output directory (default: /tmp/...)
  --rom PATH         ROM path (default: ./baserom.u.z64)
  --binary PATH      native binary path (default: build/ge007)
  --build-dir DIR    CMake build directory (default: build)
  --no-build         reuse an existing native binary
  --frame N          deterministic screenshot frame (default: 900)
  --timeout SECONDS  per-capture timeout (default: 240)

Captured screenshots, traces, logs, and saves are ROM-derived local artifacts.
Do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --frame) FRAME="$2"; shift 2 ;;
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
    echo "SKIP: intro_visual_regression: ROM not found ($ROM); ROM-gated lane."
    exit 0
fi

if ! python3 -c "import PIL" >/dev/null 2>&1; then
    echo "SKIP: intro_visual_regression: Pillow not installed (pip install pillow)."
    exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi
validation_require_binary "$BINARY"

mkdir -p "$OUT_DIR"
# The game drops screenshot_introviz.bmp in the CWD (repo root); make sure a run
# that dies between capture and conversion doesn't leave it behind.
trap 'rm -rf "$OUT_DIR"; rm -f screenshot_introviz.bmp' EXIT

# capture <name> [extra env assignments...]
capture() {
    local name="$1"; shift
    local d="$OUT_DIR/$name"
    mkdir -p "$d/save"
    if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
            GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=5 \
            "$@" \
            "$BINARY" \
            --savedir "$d/save" --rom "$ROM" --level "$LEVEL" --deterministic \
            --trace-state "$d/trace.jsonl" \
            --screenshot-frame "$FRAME" --screenshot-label introviz \
            --screenshot-exit >"$d/run.log" 2>&1; then
        echo "FAIL: capture $name failed"; tail -20 "$d/run.log" | sed 's/^/  /'; return 1
    fi
    # screenshot lands in CWD as screenshot_<label>.bmp
    local bmp="screenshot_introviz.bmp"
    if [[ ! -f "$bmp" ]]; then
        echo "FAIL: capture $name produced no screenshot"; return 1
    fi
    python3 -c "from PIL import Image; Image.open('$bmp').convert('RGB').save('$d/f.png')"
    rm -f "$bmp"
    return 0
}

# analyze <name> -> prints verdict, returns analyzer exit (0 pass / 1 fail)
analyze() {
    local name="$1"; shift
    local d="$OUT_DIR/$name"
    python3 tools/analyze_intro_body.py \
        --screenshot "$d/f.png" --trace "$d/trace.jsonl" --label "$name" "$@"
}

expect() {
    # expect PASS <name> | expect FAIL <axis> <name>   (axis = presence|grounding|shards|structure)
    # A FAIL expectation asserts the failure fired on the intended AXIS, not just
    # any failure -- e.g. a drifted presence fixture must not let the grounding
    # control "pass" its expectation for the wrong reason.
    local want="$1"; shift
    local axis=""
    if [[ "$want" == "FAIL" ]]; then
        axis="$1"; shift
    fi
    local name="$1"; shift
    local rc=0
    local output
    output="$(analyze "$name" "$@")" || rc=$?
    printf '%s\n' "$output"
    if [[ "$want" == "PASS" && "$rc" -ne 0 ]]; then
        echo "FAIL: $name expected PASS but validator failed"; return 1
    fi
    if [[ "$want" == "FAIL" ]]; then
        if [[ "$rc" -eq 0 ]]; then
            echo "FAIL: $name expected a $axis failure but validator passed"; return 1
        fi
        if ! printf '%s\n' "$output" | grep -q "^  - ${axis}:"; then
            echo "FAIL: $name failed, but not on the expected $axis axis"; return 1
        fi
        echo "OK: $name -> FAIL on $axis (as expected)"
        return 0
    fi
    echo "OK: $name -> $want (as expected)"
    return 0
}

echo "=== intro_visual_regression (Dam intro, frame $FRAME) ==="

capture normal
capture nobody       GE007_NO_BOND_BODY_FIX=1
capture offset       GE007_INTRO_BODY_Y_OFFSET=300
capture nophase3     GE007_NO_INTRO_PHASE3=1
capture norootmotion GE007_NO_INTRO_ROOTMOTION=1

rc=0
expect PASS normal                 || rc=1
expect FAIL presence  nobody       || rc=1   # aliased/absent viewer body
expect FAIL grounding offset       || rc=1   # floated root
expect PASS nophase3               || rc=1
expect PASS norootmotion           || rc=1

# Shard-detector self-test: the intro shard bug is fixed at the source and has no
# A/B hatch, so inject the dark-red shard signature into the good frame and prove
# the shard check fires. Grounding/presence come from the untouched trace/region.
python3 - "$OUT_DIR/normal/f.png" "$OUT_DIR/normal/shards.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load()
# a scatter of dark-red degenerate-shard pixels away from the body region
for i in range(400):
    x = 20 + (i * 7) % 300
    y = 90 + (i * 11) % 120
    px[x, y] = (150, 20, 20)
im.save(sys.argv[2])
PY
shard_rc=0
shard_out="$(python3 tools/analyze_intro_body.py --screenshot "$OUT_DIR/normal/shards.png" \
    --trace "$OUT_DIR/normal/trace.jsonl" --label "shard-selftest")" || shard_rc=$?
printf '%s\n' "$shard_out"
if [[ "$shard_rc" -eq 0 ]]; then
    echo "FAIL: shard self-test: injected red shards were not detected"; rc=1
elif ! printf '%s\n' "$shard_out" | grep -q "^  - shards:"; then
    echo "FAIL: shard self-test failed, but not on the shards axis"; rc=1
else
    echo "OK: shard-selftest -> FAIL on shards (as expected)"
fi

if [[ "$rc" -ne 0 ]]; then
    echo "FAIL: intro_visual_regression"
    exit 1
fi
echo "PASS: intro_visual_regression"
