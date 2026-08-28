#!/bin/bash
#
# damage_hud_smoke.sh -- Verify Bond damage rings render on HUD-visible levels.
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
TIMEOUT_SECONDS=60
OUT_DIR="/tmp/mgb64_damage_hud_smoke_$$"
LEVELS="33 36 34"
FRAMES=306
DAMAGE_FRAME=300
DAMAGE_AMOUNT="0.25"
MIN_HUD_TRIS=0
MIN_WARM_PIXELS=500
MIN_COOL_PIXELS=220

usage() {
    cat <<'USAGE'
Usage: tools/damage_hud_smoke.sh [options]

Options:
  --level LIST             raw LEVELID list, quoted if multiple (default: "33 36 34")
                           33=Dam, 36=Surface 1, 34=Facility
  --frames N               screenshot/exit frame (default: 306)
  --damage-frame N         deterministic damage injection frame (default: 300)
  --damage-amount F        Bond damage amount (default: 0.25)
  --min-hud-tris N         minimum HUD-class triangles near capture (default: 0/off)
  --min-warm-pixels N      minimum warm health-ring pixels in screenshot (default: 500)
  --min-cool-pixels N      minimum cool armor-ring pixels in screenshot (default: 220)
  --out-dir DIR            output directory (default: /tmp/...)
  --rom PATH               ROM path (default: ./baserom.u.z64)
  --binary PATH            native binary path (default: build/ge007)
  --build-dir DIR          CMake build directory (default: build)
  --no-build               reuse an existing native binary
  --timeout SECONDS        per-level timeout (default: 60)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVELS="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --damage-frame) DAMAGE_FRAME="$2"; shift 2 ;;
        --damage-amount) DAMAGE_AMOUNT="$2"; shift 2 ;;
        --min-hud-tris) MIN_HUD_TRIS="$2"; shift 2 ;;
        --min-warm-pixels) MIN_WARM_PIXELS="$2"; shift 2 ;;
        --min-cool-pixels) MIN_COOL_PIXELS="$2"; shift 2 ;;
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

for value_name in FRAMES DAMAGE_FRAME TIMEOUT_SECONDS MIN_WARM_PIXELS MIN_COOL_PIXELS; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: ${value_name} must be a positive integer: $value" >&2
        exit 2
    fi
done
if [[ ! "$MIN_HUD_TRIS" =~ ^[0-9]+$ ]]; then
    echo "FAIL: MIN_HUD_TRIS must be a non-negative integer: $MIN_HUD_TRIS" >&2
    exit 2
fi
if [[ "$DAMAGE_FRAME" -ge "$FRAMES" ]]; then
    echo "FAIL: --damage-frame must be before --frames" >&2
    exit 2
fi
if ! python3 - "$DAMAGE_AMOUNT" <<'PY'
import math
import sys

try:
    value = float(sys.argv[1])
except ValueError:
    raise SystemExit(1)
raise SystemExit(0 if math.isfinite(value) and value > 0.0 and value < 1.0 else 1)
PY
then
    echo "FAIL: --damage-amount must be a finite value between 0 and 1: $DAMAGE_AMOUNT" >&2
    exit 2
fi
for lvl in $LEVELS; do
    if [[ ! "$lvl" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: --level list contains a non-integer LEVELID: $lvl" >&2
        exit 2
    fi
done

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

SUMMARY_FILE="$OUT_DIR/summary.tsv"
printf 'level\tstatus\twarm_pixels\tcool_pixels\thud_tris\tactive_frame\n' >"$SUMMARY_FILE"

FAILED=0
PASSED=0
TOTAL=0

run_level() {
    local lvl="$1"
    local label="damage_hud_${lvl}_$$"
    local trace="$OUT_DIR/level_${lvl}.jsonl"
    local log="$OUT_DIR/level_${lvl}.log"
    local screenshot_src="$OUT_DIR/screenshot_${label}.bmp"
    local screenshot_dst="$OUT_DIR/level_${lvl}.bmp"
    local screenshot_log="$OUT_DIR/level_${lvl}.screenshot.txt"
    local screenshot_json="$OUT_DIR/level_${lvl}.screenshot.json"
    local render_log="$OUT_DIR/level_${lvl}.render.txt"
    local render_json="$OUT_DIR/level_${lvl}.render.json"
    local damage_log="$OUT_DIR/level_${lvl}.damage_hud.txt"
    local damage_json="$OUT_DIR/level_${lvl}.damage_hud.json"
    local assert_count
    local warm
    local cool
    local hud
    local active_frame

    rm -f "$trace" "$log" "$screenshot_src" "$screenshot_dst" \
        "$screenshot_log" "$screenshot_json" "$render_log" "$render_json" \
        "$damage_log" "$damage_json"

    if ! (
        cd "$OUT_DIR"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
                SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
                GE007_MUTE=1 \
                GE007_DETERMINISTIC_STABLE_COUNT=1 \
                GE007_NO_VSYNC=1 \
                GE007_BACKGROUND=1 \
                GE007_NO_INPUT_GRAB=1 \
                GE007_DISABLE_LEVEL_INTRO=1 \
                GE007_TRACE_DRAWCLASS_TRIS=1 \
                GE007_TRACE_DRAWCLASS_AFTER_FRAME="$((FRAMES - 2))" \
                GE007_AUTO_DAMAGE_BOND_FRAME="$DAMAGE_FRAME" \
                GE007_AUTO_DAMAGE_BOND_AMOUNT="$DAMAGE_AMOUNT" \
                GE007_AUTO_DAMAGE_BOND_ANGLE=0 \
                GE007_AUTO_DAMAGE_BOND_AFFECTS_ARMOR=1 \
                "$BINARY" \
                --rom "$ROM" \
                --level "$lvl" \
                --deterministic \
                --trace-state "$trace" \
                --screenshot-frame "$FRAMES" \
                --screenshot-label "$label" \
                --screenshot-exit
    ) >"$log" 2>&1; then
        echo "    process: FAIL"
        tail -20 "$log" | sed 's/^/      /'
        printf '%s\tfail\t-\t-\t-\t-\n' "$lvl" >>"$SUMMARY_FILE"
        return 1
    fi
    echo "    process: PASS"

    assert_count="$(grep -cF "[GEASSERT]" "$log" 2>/dev/null || true)"
    assert_count="${assert_count:-0}"
    if [[ "$assert_count" -ne 0 ]]; then
        echo "    assertions: FAIL ($assert_count)"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/      /'
        printf '%s\tfail\t-\t-\t-\t-\n' "$lvl" >>"$SUMMARY_FILE"
        return 1
    fi
    echo "    assertions: PASS"

    if [[ -f "$screenshot_src" ]]; then
        mv "$screenshot_src" "$screenshot_dst"
        echo "    screenshot: PASS"
    else
        echo "    screenshot: FAIL (missing)"
        printf '%s\tfail\t-\t-\t-\t-\n' "$lvl" >>"$SUMMARY_FILE"
        return 1
    fi

    if python3 tools/audit_screenshot_health.py \
        --label "damage HUD level $lvl screenshot" \
        --expect-size 640x480 \
        --json-out "$screenshot_json" \
        "$screenshot_dst" >"$screenshot_log" 2>&1; then
        echo "    screenshot_health: PASS"
    else
        echo "    screenshot_health: FAIL"
        sed -n '1,12p' "$screenshot_log" | sed 's/^/      /'
        printf '%s\tfail\t-\t-\t-\t-\n' "$lvl" >>"$SUMMARY_FILE"
        return 1
    fi

    if python3 tools/audit_render_trace.py \
        --label "damage HUD level $lvl" \
        --json-out "$render_json" \
        "$trace" >"$render_log" 2>&1; then
        echo "    render_health: PASS"
    else
        echo "    render_health: FAIL"
        sed -n '1,12p' "$render_log" | sed 's/^/      /'
        printf '%s\tfail\t-\t-\t-\t-\n' "$lvl" >>"$SUMMARY_FILE"
        return 1
    fi

    if python3 tools/audit_damage_hud_capture.py \
        --label "damage HUD level $lvl" \
        --screenshot "$screenshot_dst" \
        --trace "$trace" \
        --log "$log" \
        --damage-frame "$DAMAGE_FRAME" \
        --screenshot-frame "$FRAMES" \
        --min-hud-tris "$MIN_HUD_TRIS" \
        --min-warm-pixels "$MIN_WARM_PIXELS" \
        --min-cool-pixels "$MIN_COOL_PIXELS" \
        --json-out "$damage_json" >"$damage_log" 2>&1; then
        echo "    damage_hud: PASS"
    else
        echo "    damage_hud: FAIL"
        sed -n '1,12p' "$damage_log" | sed 's/^/      /'
        printf '%s\tfail\t-\t-\t-\t-\n' "$lvl" >>"$SUMMARY_FILE"
        return 1
    fi

    warm="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["pixels"]["warm_pixels"])' "$damage_json")"
    cool="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["pixels"]["cool_pixels"])' "$damage_json")"
    hud="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["drawclass"]["max_hud_tris"])' "$damage_json")"
    active_frame="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["trace"]["active_damage_record"]["frame"])' "$damage_json")"
    printf '%s\tpass\t%s\t%s\t%s\t%s\n' "$lvl" "$warm" "$cool" "$hud" "$active_frame" >>"$SUMMARY_FILE"
    return 0
}

for lvl in $LEVELS; do
    TOTAL=$((TOTAL + 1))
    echo "== damage HUD level $lvl =="
    if run_level "$lvl"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
done

echo
echo "damage HUD smoke summary: passed=$PASSED failed=$FAILED total=$TOTAL"
echo "artifacts: $OUT_DIR"
cat "$SUMMARY_FILE" | sed 's/^/  /'

if [[ "$FAILED" -ne 0 ]]; then
    exit 1
fi
