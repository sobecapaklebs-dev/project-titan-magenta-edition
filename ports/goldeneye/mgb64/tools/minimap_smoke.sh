#!/bin/bash
#
# minimap_smoke.sh -- Validate the native minimap cache, objective pins, overlay,
# and disabled-path behavior.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, logs, or generated audit summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_minimap_smoke_$$"
FRAMES=180
LEVELS="33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32"
TOGGLE_LEVELS=""
RUN_TOGGLE=1
SCREEN_WIDTH=640
SCREEN_HEIGHT=480

usage() {
    cat <<'USAGE'
Usage: tools/minimap_smoke.sh [options]

Options:
  --level LIST          raw LEVELID list, quoted if multiple (default: all 20 solo stages)
  --toggle-level LIST   disabled-path LEVELID list (default: same as --level)
  --no-toggle           skip disabled-path parity checks
  --frames N            screenshot/exit frame (default: 180)
  --out-dir DIR         output directory (default: /tmp/...)
  --rom PATH            ROM path (default: ./baserom.u.z64)
  --binary PATH         native binary path (default: build/ge007)
  --build-dir DIR       CMake build directory (default: build)
  --no-build            reuse an existing native binary
  --timeout SECONDS     per-capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVELS="$2"; shift 2 ;;
        --toggle-level) TOGGLE_LEVELS="$2"; shift 2 ;;
        --no-toggle) RUN_TOGGLE=0; shift ;;
        --frames) FRAMES="$2"; shift 2 ;;
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

if [[ -z "${TOGGLE_LEVELS//[[:space:]]/}" ]]; then
    TOGGLE_LEVELS="$LEVELS"
fi
for pair in "frames:$FRAMES" "timeout:$TIMEOUT_SECONDS"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: --$name must be a positive integer: $value" >&2
        exit 2
    fi
done
for lvl in $LEVELS $TOGGLE_LEVELS; do
    if [[ ! "$lvl" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: level list contains a non-integer LEVELID: $lvl" >&2
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

SUMMARY_JSON="$OUT_DIR/summary.json"
SUMMARY_TSV="$OUT_DIR/summary.tsv"
printf 'level\tcase\tstatus\tminimap_json\toverlay_json\ttrace_json\n' >"$SUMMARY_TSV"

FAILED=0
PASSED=0

run_enabled_case() {
    local lvl="$1"
    local case_dir="$OUT_DIR/level_${lvl}_enabled"
    local label="minimap_${lvl}_enabled_$$"
    local trace="$case_dir/trace.jsonl"
    local log="$case_dir/run.log"
    local minimap_json="$case_dir/minimap.json"
    local overlay_json="$case_dir/overlay.json"
    local stage_dump="$case_dir/stage_dump.jsonl"
    local screenshot_src="$case_dir/screenshot_${label}.bmp"
    local screenshot_dst="$case_dir/screenshot.bmp"
    local render_json="$case_dir/render.json"
    local screenshot_json="$case_dir/screenshot.json"
    local minimap_audit_json="$case_dir/minimap_audit.json"

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$minimap_json" "$overlay_json" "$stage_dump" \
          "$screenshot_src" "$screenshot_dst" "$render_json" "$screenshot_json" "$minimap_audit_json"

    echo "  enabled level $lvl"
    if ! (
        cd "$case_dir"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
                SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
                GE007_MUTE=1 \
                GE007_DETERMINISTIC_STABLE_COUNT=1 \
                GE007_NO_VSYNC=1 \
                GE007_BACKGROUND=1 \
                GE007_NO_INPUT_GRAB=1 \
                GE007_DISABLE_LEVEL_INTRO=1 \
                GE007_MINIMAP_DUMP="$minimap_json" \
                GE007_MINIMAP_OVERLAY_DUMP="$overlay_json" \
                GE007_DUMP_STAGE_PADS="$stage_dump" \
                GE007_DUMP_STAGE_PADS_FRAME=2 \
                "$BINARY" \
                --savedir "$case_dir/save" \
                --rom "$ROM" \
                --level "$lvl" \
                --deterministic \
                --trace-state "$trace" \
                --screenshot-frame "$FRAMES" \
                --screenshot-label "$label" \
                --screenshot-exit \
                --config-override Video.WindowWidth="$SCREEN_WIDTH" \
                --config-override Video.WindowHeight="$SCREEN_HEIGHT" \
                --config-override Video.WindowMode=windowed \
                --config-override Video.WindowX=-1 \
                --config-override Video.WindowY=-1 \
                --config-override Input.MinimapEnabled=1 \
                --config-override Input.MinimapSharpOverlay=1 \
                --config-override Input.MinimapObjectives=1
    ) >"$log" 2>&1; then
        echo "    process: FAIL"
        tail -60 "$log" | sed 's/^/      /'
        return 1
    fi
    echo "    process: PASS"

    if grep -qF "[GEASSERT]" "$log"; then
        echo "    assertions: FAIL"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/      /'
        return 1
    fi
    echo "    assertions: PASS"

    if [[ -f "$screenshot_src" ]]; then
        mv "$screenshot_src" "$screenshot_dst"
    fi
    if [[ ! -s "$screenshot_dst" ]]; then
        echo "    screenshot: FAIL"
        return 1
    fi
    echo "    screenshot: PASS"

    if python3 tools/audit_screenshot_health.py \
        --label "minimap enabled level $lvl screenshot" \
        --expect-size "${SCREEN_WIDTH}x${SCREEN_HEIGHT}" \
        --json-out "$screenshot_json" \
        "$screenshot_dst" >"$case_dir/screenshot_audit.txt" 2>&1; then
        echo "    screenshot_health: PASS"
    else
        echo "    screenshot_health: FAIL"
        sed -n '1,12p' "$case_dir/screenshot_audit.txt" | sed 's/^/      /'
        return 1
    fi

    if python3 tools/audit_render_trace.py \
        --label "minimap enabled level $lvl render" \
        --json-out "$render_json" \
        "$trace" >"$case_dir/render_audit.txt" 2>&1; then
        echo "    render_health: PASS"
    else
        echo "    render_health: FAIL"
        sed -n '1,12p' "$case_dir/render_audit.txt" | sed 's/^/      /'
        return 1
    fi

    if python3 tools/audit_minimap_dump.py \
        --label "minimap enabled level $lvl" \
        --minimap-dump "$minimap_json" \
        --stage-dump "$stage_dump" \
        --overlay-dump "$overlay_json" \
        --expect-stage "$lvl" \
        --require-ready \
        --expect-snapshot \
        --require-objective-pins-from-stage \
        --require-overlay-drawn \
        --json-out "$minimap_audit_json" >"$case_dir/minimap_audit.txt" 2>&1; then
        echo "    minimap: PASS"
    else
        echo "    minimap: FAIL"
        sed -n '1,20p' "$case_dir/minimap_audit.txt" | sed 's/^/      /'
        return 1
    fi

    printf '%s\tenabled\tpass\t%s\t%s\t%s\n' "$lvl" "$minimap_json" "$overlay_json" "$trace" >>"$SUMMARY_TSV"
    return 0
}

run_disabled_case() {
    local lvl="$1"
    local enabled_ref="$OUT_DIR/level_${lvl}_enabled/minimap.json"
    local case_dir="$OUT_DIR/level_${lvl}_disabled"
    local trace="$case_dir/trace.jsonl"
    local log="$case_dir/run.log"
    local minimap_json="$case_dir/minimap.json"
    local overlay_json="$case_dir/overlay.json"
    local stage_dump="$case_dir/stage_dump.jsonl"
    local render_json="$case_dir/render.json"
    local minimap_audit_json="$case_dir/minimap_audit.json"

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$minimap_json" "$overlay_json" "$stage_dump" "$render_json" "$minimap_audit_json"

    echo "  disabled level $lvl"
    if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_MINIMAP_DUMP="$minimap_json" \
            GE007_MINIMAP_OVERLAY_DUMP="$overlay_json" \
            GE007_DUMP_STAGE_PADS="$stage_dump" \
            GE007_DUMP_STAGE_PADS_FRAME=2 \
            GE007_AUTO_EXIT_FRAME="$FRAMES" \
            "$BINARY" \
            --savedir "$case_dir/save" \
            --rom "$ROM" \
            --level "$lvl" \
            --deterministic \
            --trace-state "$trace" \
            --config-override Video.WindowWidth="$SCREEN_WIDTH" \
            --config-override Video.WindowHeight="$SCREEN_HEIGHT" \
            --config-override Video.WindowMode=windowed \
            --config-override Video.WindowX=-1 \
            --config-override Video.WindowY=-1 \
            --config-override Input.MinimapEnabled=0 \
            --config-override Input.MinimapSharpOverlay=1 \
            --config-override Input.MinimapObjectives=1 >"$log" 2>&1; then
        echo "    process: FAIL"
        tail -60 "$log" | sed 's/^/      /'
        return 1
    fi
    echo "    process: PASS"

    if grep -qF "[GEASSERT]" "$log"; then
        echo "    assertions: FAIL"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/      /'
        return 1
    fi
    echo "    assertions: PASS"

    if ! grep -qF "deterministic frame exit observed" "$log"; then
        echo "    exit_marker: FAIL"
        tail -30 "$log" | sed 's/^/      /'
        return 1
    fi
    echo "    exit_marker: PASS"

    if python3 tools/audit_render_trace.py \
        --label "minimap disabled level $lvl render" \
        --json-out "$render_json" \
        "$trace" >"$case_dir/render_audit.txt" 2>&1; then
        echo "    render_health: PASS"
    else
        echo "    render_health: FAIL"
        sed -n '1,12p' "$case_dir/render_audit.txt" | sed 's/^/      /'
        return 1
    fi

    if python3 tools/audit_minimap_dump.py \
        --label "minimap disabled level $lvl" \
        --minimap-dump "$minimap_json" \
        --stage-dump "$stage_dump" \
        --overlay-dump "$overlay_json" \
        --reference-minimap "$enabled_ref" \
        --expect-stage "$lvl" \
        --require-ready \
        --expect-no-snapshot \
        --expect-overlay-no-queue \
        --json-out "$minimap_audit_json" >"$case_dir/minimap_audit.txt" 2>&1; then
        echo "    minimap_disabled: PASS"
    else
        echo "    minimap_disabled: FAIL"
        sed -n '1,20p' "$case_dir/minimap_audit.txt" | sed 's/^/      /'
        return 1
    fi

    printf '%s\tdisabled\tpass\t%s\t%s\t%s\n' "$lvl" "$minimap_json" "$overlay_json" "$trace" >>"$SUMMARY_TSV"
    return 0
}

echo "=== Minimap Smoke ==="
echo "  out-dir:       $OUT_DIR"
echo "  binary:        $BINARY"
echo "  ROM:           $ROM"
echo "  levels:        $LEVELS"
echo "  toggle-levels: $TOGGLE_LEVELS"
echo "  frames:        $FRAMES"

for lvl in $LEVELS; do
    if run_enabled_case "$lvl"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        printf '%s\tenabled\tfail\t\t\t\n' "$lvl" >>"$SUMMARY_TSV"
    fi
done

if [[ "$RUN_TOGGLE" -eq 1 ]]; then
    for lvl in $TOGGLE_LEVELS; do
        if [[ ! -s "$OUT_DIR/level_${lvl}_enabled/minimap.json" ]]; then
            echo "  disabled level $lvl: SKIP (missing enabled reference)"
            continue
        fi
        if run_disabled_case "$lvl"; then
            PASSED=$((PASSED + 1))
        else
            FAILED=$((FAILED + 1))
            printf '%s\tdisabled\tfail\t\t\t\n' "$lvl" >>"$SUMMARY_TSV"
        fi
    done
fi

python3 - "$SUMMARY_TSV" "$SUMMARY_JSON" "$PASSED" "$FAILED" "$LEVELS" "$TOGGLE_LEVELS" "$FRAMES" <<'PY'
import csv
import json
import sys
from pathlib import Path

summary_tsv = Path(sys.argv[1])
summary_json = Path(sys.argv[2])
passed = int(sys.argv[3])
failed = int(sys.argv[4])
levels = [int(item) for item in sys.argv[5].split()]
toggle_levels = [int(item) for item in sys.argv[6].split()]
frames = int(sys.argv[7])

rows = []
with summary_tsv.open("r", encoding="utf-8", newline="") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        rows.append(row)

summary = {
    "status": "fail" if failed else "pass",
    "summary_tsv": str(summary_tsv),
    "counts": {
        "passed": passed,
        "failed": failed,
        "rows": len(rows),
    },
    "config": {
        "levels": levels,
        "toggle_levels": toggle_levels,
        "frames": frames,
    },
    "runs": rows,
}
summary_json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
print(f"summary_json: {summary_json}")
PY

echo ""
echo "=== Minimap Smoke: $PASSED passed, $FAILED failed ==="
echo "  artifacts: $OUT_DIR"
echo "  summary:   $SUMMARY_JSON"
exit "$FAILED"
