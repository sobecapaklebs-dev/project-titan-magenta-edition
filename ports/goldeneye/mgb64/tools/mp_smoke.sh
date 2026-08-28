#!/bin/bash
#
# mp_smoke.sh -- Split-screen multiplayer deathmatch boot + render smoke.
#
# Boots a 2-player split-screen deathmatch via the native --multiplayer CLI
# flags, headless and deterministic, drives a short scripted P1 input window,
# captures a 640x480 screenshot, and asserts the two split-screen halves
# (top = P1, bottom = P2, split at SCREEN_HEIGHT/2) are measurably DISSIMILAR.
# A duplicated-camera regression (both halves identical) must fail this lane.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, logs, or audit summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_mp_smoke_$$"
FRAMES=300
INPUT_WINDOW="80:160"
PLAYERS=2
MP_STAGE="temple"
SCENARIO="deathmatch"
SCREEN_WIDTH=640
SCREEN_HEIGHT=480
MIN_HALF_DELTA_PCT="2.0"
TIMELIMIT_SECS=0

usage() {
    cat <<'USAGE'
Usage: tools/mp_smoke.sh [options]

Options:
  --players N              split-screen player count, 2-4 (default: 2)
  --mp-stage NAME|ID       multiplayer stage slug or index (default: temple)
  --scenario NAME|ID       combat scenario (default: deathmatch)
  --input-window START:LEN deterministic P1 GE007_AUTO_* window (default: 80:160)
  --frames N               deterministic screenshot/exit frame (default: 300)
  --timelimit SECS         force a custom match time limit and assert the match
                           timer elapses to it crash-free. Raises --frames to
                           at least SECS*60 + 120.
  --min-half-delta-pct F   minimum changed-pixel %% between the two halves (default: 2.0)
  --out-dir DIR            output directory (default: /tmp/...)
  --rom PATH               ROM path (default: ./baserom.u.z64)
  --binary PATH            native binary path (default: build/ge007)
  --build-dir DIR          CMake build directory (default: build)
  --no-build               reuse an existing native binary
  --timeout SECONDS        per-attempt timeout (default: 120)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --players) PLAYERS="$2"; shift 2 ;;
        --mp-stage) MP_STAGE="$2"; shift 2 ;;
        --scenario) SCENARIO="$2"; shift 2 ;;
        --input-window) INPUT_WINDOW="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --timelimit) TIMELIMIT_SECS="$2"; shift 2 ;;
        --min-half-delta-pct) MIN_HALF_DELTA_PCT="$2"; shift 2 ;;
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

if [[ ! "$PLAYERS" =~ ^[2-4]$ ]]; then
    echo "FAIL: --players must be 2, 3, or 4: $PLAYERS" >&2
    exit 2
fi
if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frames must be a positive integer: $FRAMES" >&2
    exit 2
fi
if [[ ! "$TIMELIMIT_SECS" =~ ^[0-9]+$ ]]; then
    echo "FAIL: --timelimit must be a non-negative integer (seconds): $TIMELIMIT_SECS" >&2
    exit 2
fi
TIMELIMIT_TICKS=0
if (( TIMELIMIT_SECS > 0 )); then
    TIMELIMIT_TICKS=$(( TIMELIMIT_SECS * 60 ))
    NEEDED_FRAMES=$(( TIMELIMIT_TICKS + 120 ))
    if (( FRAMES < NEEDED_FRAMES )); then
        FRAMES=$NEEDED_FRAMES
    fi
    NEEDED_TIMEOUT=$(( TIMELIMIT_SECS + 120 ))
    if (( TIMEOUT_SECONDS < NEEDED_TIMEOUT )); then
        TIMEOUT_SECONDS=$NEEDED_TIMEOUT
    fi
fi
if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi
if [[ ! "$INPUT_WINDOW" =~ ^[0-9]+:[1-9][0-9]*(,[0-9]+:[1-9][0-9]*)*$ ]]; then
    echo "FAIL: --input-window must use START:LEN windows: $INPUT_WINDOW" >&2
    exit 2
fi
if ! python3 - "$MIN_HALF_DELTA_PCT" <<'PY'
import math
import sys

try:
    value = float(sys.argv[1])
except ValueError:
    raise SystemExit(1)
raise SystemExit(0 if math.isfinite(value) and value >= 0.0 else 1)
PY
then
    echo "FAIL: --min-half-delta-pct must be a non-negative finite number: $MIN_HALF_DELTA_PCT" >&2
    exit 2
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

TIMEOUT_CMD="$(validation_resolve_timeout_cmd)"
if [[ -n "$TIMEOUT_CMD" ]]; then
    TIMEOUT_BIN="$TIMEOUT_CMD"
    TIMEOUT_ARGS=(--kill-after=5 "$TIMEOUT_SECONDS")
else
    TIMEOUT_BIN="env"
    TIMEOUT_ARGS=()
fi

LABEL="mp_smoke_${PLAYERS}p_$$"
TRACE="$OUT_DIR/mp.jsonl"
LOG="$OUT_DIR/mp.log"
RENDER_LOG="$OUT_DIR/mp.render.txt"
RENDER_JSON="$OUT_DIR/mp.render.json"
SCREENSHOT_SRC="$OUT_DIR/screenshot_${LABEL}.bmp"
SCREENSHOT_DST="$OUT_DIR/mp.bmp"
SCREENSHOT_LOG="$OUT_DIR/mp.screenshot.txt"
SCREENSHOT_JSON="$OUT_DIR/mp.screenshot.json"
TOP_HALF="$OUT_DIR/mp_top.png"
BOTTOM_HALF="$OUT_DIR/mp_bottom.png"
HALVES_LOG="$OUT_DIR/mp.halves.txt"
HALVES_JSON="$OUT_DIR/mp.halves.json"

echo "=== Multiplayer Split-Screen Smoke ==="
echo "  out-dir:             $OUT_DIR"
echo "  binary:              $BINARY"
echo "  ROM:                 $ROM"
echo "  players:             $PLAYERS"
echo "  mp-stage:            $MP_STAGE"
echo "  scenario:            $SCENARIO"
echo "  input-window:        $INPUT_WINDOW"
echo "  frames:              $FRAMES"
echo "  min-half-delta-pct:  $MIN_HALF_DELTA_PCT"
if (( TIMELIMIT_SECS > 0 )); then
    echo "  timelimit:           ${TIMELIMIT_SECS}s (${TIMELIMIT_TICKS} ticks)"
fi

MP_EXTRA_ARGS=()
if (( TIMELIMIT_SECS > 0 )); then
    MP_EXTRA_ARGS+=(--mp-timelimit "$TIMELIMIT_SECS")
fi

# Boot the split-screen deathmatch and drive P1 forward+right so the P1 half
# (top) diverges from the static P2 half (bottom). Identical halves -- the
# duplicated-camera bug -- fail the dissimilarity assertion below.
if ! (
    cd "$OUT_DIR"
    env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1 \
        GE007_DEBUG=1 \
        GE007_ASSERT_ON_FAIL=0 \
        GE007_AUTO_FORWARD="$INPUT_WINDOW" \
        GE007_AUTO_RIGHT="$INPUT_WINDOW" \
        "$TIMEOUT_BIN" "${TIMEOUT_ARGS[@]}" \
        "$BINARY" \
        --rom "$ROM" \
        --savedir "$OUT_DIR" \
        --multiplayer \
        --players "$PLAYERS" \
        --mp-stage "$MP_STAGE" \
        --scenario "$SCENARIO" \
        ${MP_EXTRA_ARGS[@]+"${MP_EXTRA_ARGS[@]}"} \
        --deterministic \
        --trace-state "$TRACE" \
        --screenshot-frame "$FRAMES" \
        --screenshot-label "$LABEL" \
        --screenshot-exit
) >"$LOG" 2>&1; then
    echo "  process: FAIL"
    tail -20 "$LOG" | sed 's/^/    /'
    exit 1
fi
echo "  process: PASS"

if grep -qF "[GEASSERT]" "$LOG" 2>/dev/null; then
    echo "  assertions: FAIL"
    grep -F "[GEASSERT]" "$LOG" | head -5 | sed 's/^/    /'
    exit 1
fi
echo "  assertions: PASS"

if [[ ! -s "$TRACE" ]]; then
    echo "  trace: FAIL (missing or empty)"
    tail -20 "$LOG" | sed 's/^/    /'
    exit 1
fi
echo "  trace: PASS"

if python3 tools/audit_render_trace.py \
    --label "mp smoke ${PLAYERS}p $MP_STAGE $SCENARIO" \
    --max-crashes 0 \
    --max-bad-cmds 0 \
    --max-nan 0 \
    --max-dl-counter 0 \
    --json-out "$RENDER_JSON" \
    "$TRACE" >"$RENDER_LOG" 2>&1; then
    echo "  render_health: PASS"
else
    echo "  render_health: FAIL"
    sed -n '1,16p' "$RENDER_LOG" | sed 's/^/    /'
    exit 1
fi

if [[ -f "$SCREENSHOT_SRC" ]]; then
    mv "$SCREENSHOT_SRC" "$SCREENSHOT_DST"
    echo "  screenshot: PASS"
else
    echo "  screenshot: FAIL (missing)"
    exit 1
fi

if python3 tools/audit_screenshot_health.py \
    --label "mp smoke ${PLAYERS}p split-screen screenshot" \
    --expect-size "${SCREEN_WIDTH}x${SCREEN_HEIGHT}" \
    --json-out "$SCREENSHOT_JSON" \
    "$SCREENSHOT_DST" >"$SCREENSHOT_LOG" 2>&1; then
    echo "  screenshot_health: PASS"
else
    echo "  screenshot_health: FAIL"
    sed -n '1,12p' "$SCREENSHOT_LOG" | sed 's/^/    /'
    exit 1
fi

# Crop the framebuffer into its top (P1) and bottom (P2) halves at
# SCREEN_HEIGHT/2 so the existing compare tool can score the divergence.
if ! python3 - "$SCREENSHOT_DST" "$TOP_HALF" "$BOTTOM_HALF" "$SCREEN_WIDTH" "$SCREEN_HEIGHT" <<'PY'
import sys

from PIL import Image

src, top_path, bottom_path, width, height = sys.argv[1:6]
width = int(width)
height = int(height)
image = Image.open(src).convert("RGB")
if image.size != (width, height):
    print(f"FAIL: screenshot size {image.size} != ({width}, {height})")
    raise SystemExit(1)
half = height // 2
image.crop((0, 0, width, half)).save(top_path)
image.crop((0, half, width, 2 * half)).save(bottom_path)
raise SystemExit(0)
PY
then
    echo "  split_crop: FAIL"
    exit 1
fi
echo "  split_crop: PASS"

if ! python3 tools/compare_screenshots.py \
    --json-out "$HALVES_JSON" \
    "$TOP_HALF" "$BOTTOM_HALF" >"$HALVES_LOG" 2>&1; then
    echo "  half_compare: FAIL (comparison error)"
    sed -n '1,12p' "$HALVES_LOG" | sed 's/^/    /'
    exit 1
fi

# Require the two halves to be measurably DISSIMILAR: a working split-screen
# renders two distinct viewpoints, while a duplicated-camera bug produces
# near-identical halves and a near-zero changed-pixel percentage.
if python3 - "$HALVES_JSON" "$MIN_HALF_DELTA_PCT" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    metrics = json.load(handle)
threshold = float(sys.argv[2])
changed_pct = float(metrics.get("changed_pct", 0.0))
print(f"  half changed_pct={changed_pct:.3f}% (min {threshold:.3f}%)")
raise SystemExit(0 if changed_pct >= threshold else 1)
PY
then
    echo "  split_dissimilar: PASS"
else
    echo "  split_dissimilar: FAIL (halves too similar -- possible duplicated-camera bug)"
    exit 1
fi

if (( TIMELIMIT_SECS > 0 )); then
    if python3 - "$TRACE" "$TIMELIMIT_TICKS" <<'PY'
import json
import sys

trace_path = sys.argv[1]
limit = int(sys.argv[2])
peak = 0

with open(trace_path, "r", encoding="utf-8") as handle:
    for line in handle:
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except ValueError:
            continue
        ticks = rec.get("mp", {}).get("time_ticks")
        if isinstance(ticks, (int, float)) and ticks > peak:
            peak = int(ticks)

print(f"  match timer peaked at {peak}/{limit} ticks")
raise SystemExit(0 if peak >= limit else 1)
PY
    then
        echo "  match_timer_elapsed: PASS"
    else
        echo "  match_timer_elapsed: FAIL (timer never reached the configured limit)"
        exit 1
    fi
fi

echo ""
echo "=== Multiplayer Split-Screen Smoke: PASS ==="
echo "  artifacts: $OUT_DIR"
echo "  screenshot: $SCREENSHOT_DST"
exit 0
