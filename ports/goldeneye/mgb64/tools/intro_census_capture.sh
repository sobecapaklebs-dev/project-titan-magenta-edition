#!/bin/bash
#
# intro_census_capture.sh -- Capture native level-intro health/census traces.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, logs, or extracted summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=60
OUT_DIR="/tmp/mgb64_intro_census_$$"
FRAMES=1200
LEVELS="33 41"
ALL_LEVELS="33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32"
ALLOW_RENDER_FALLBACK=0

usage() {
    cat <<'USAGE'
Usage: tools/intro_census_capture.sh [options]

Options:
  --all                    capture all 20 supported solo stages
  --level LIST             raw LEVELID list, quoted if multiple (default: "33 41")
  --frames N               deterministic frames per level (default: 1200)
  --out-dir DIR            output directory (default: /tmp/...)
  --rom PATH               ROM path (default: ./baserom.u.z64)
  --binary PATH            native binary path (default: build/ge007)
  --build-dir DIR          CMake build directory (default: build)
  --no-build               reuse an existing native binary
  --timeout SECONDS        per-level timeout (default: 60)
  --allow-render-fallback  do not fail if the room renderer reports fallback

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all) LEVELS="$ALL_LEVELS"; shift ;;
        --level) LEVELS="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --allow-render-fallback) ALLOW_RENDER_FALLBACK=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frames must be a positive integer: $FRAMES" >&2
    exit 2
fi
if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
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
OUT_DIR="$(python3 - "$OUT_DIR" <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
)"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

summary_args=()
CAPTURE_INDEX="$OUT_DIR/captures.tsv"
CAPTURE_SUMMARY_JSON="$OUT_DIR/capture_summary.json"
printf 'level\ttrace\tlog\tscreenshot\tscreenshot_json\trender_json\n' >"$CAPTURE_INDEX"

echo "=== Intro Census Capture ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  levels:  $LEVELS"
echo "  frames:  $FRAMES"

for lvl in $LEVELS; do
    trace="$OUT_DIR/level_${lvl}.jsonl"
    log="$OUT_DIR/level_${lvl}.log"
    label="intro_census_${lvl}_$$"
    screenshot_src="$OUT_DIR/screenshot_${label}.bmp"
    screenshot_dst="$OUT_DIR/level_${lvl}.bmp"
    screenshot_json="$OUT_DIR/level_${lvl}.screenshot.json"
    render_json="$OUT_DIR/level_${lvl}.render.json"
    env_cmd=()

    rm -f "$trace" "$log" "$screenshot_src" "$screenshot_dst" "$screenshot_json" "$render_json"

    echo ""
    echo "=== Intro Census: Level $lvl ==="

    env_cmd=(env -u GE007_DEBUG
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
        GE007_MUTE=1
        GE007_DETERMINISTIC_STABLE_COUNT=1
        GE007_NO_VSYNC=1
        GE007_BACKGROUND=1
        GE007_NO_INPUT_GRAB=1
        GE007_ENABLE_LEVEL_INTRO=1
        "$BINARY"
        --rom "$ROM"
        --level "$lvl"
        --deterministic
        --trace-state "$trace"
        --screenshot-frame "$FRAMES"
        --screenshot-label "$label"
        --screenshot-exit)

    if ! (cd "$OUT_DIR" && validation_run_with_timeout "$TIMEOUT_SECONDS" "${env_cmd[@]}") >"$log" 2>&1; then
        echo "FAIL: intro census capture failed for level $lvl" >&2
        tail -60 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: intro census trace was not written for level $lvl: $trace" >&2
        tail -60 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$screenshot_src" ]]; then
        echo "FAIL: intro census screenshot was not written for level $lvl: $screenshot_src" >&2
        tail -60 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    mv "$screenshot_src" "$screenshot_dst"
    python3 tools/audit_screenshot_health.py \
        --label "intro census level $lvl screenshot" \
        --expect-size 640x480 \
        --json-out "$screenshot_json" \
        "$screenshot_dst"

    if [[ "$ALLOW_RENDER_FALLBACK" -eq 1 ]]; then
        python3 tools/audit_render_trace.py \
            --label "intro census level $lvl" \
            --allow-room-fallback \
            --json-out "$render_json" \
            "$trace"
    else
        python3 tools/audit_render_trace.py \
            --label "intro census level $lvl" \
            --json-out "$render_json" \
            "$trace"
    fi
    python3 tools/summarize_intro_census.py "$trace"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$lvl" \
        "$trace" \
        "$log" \
        "$screenshot_dst" \
        "$screenshot_json" \
        "$render_json" >>"$CAPTURE_INDEX"
    summary_args+=("$trace")
done

echo ""
echo "=== Intro Census Summary ==="
python3 tools/summarize_intro_census.py \
    --json-out "$OUT_DIR/summary.json" \
    "${summary_args[@]}" >"$OUT_DIR/summary.txt"
cat "$OUT_DIR/summary.txt"

python3 - "$CAPTURE_INDEX" "$OUT_DIR/summary.json" "$CAPTURE_SUMMARY_JSON" <<'PY'
import csv
import json
import sys
from pathlib import Path

capture_index = Path(sys.argv[1])
intro_summary_json = Path(sys.argv[2])
capture_summary_json = Path(sys.argv[3])


def load_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data if isinstance(data, dict) else {"value": data}


captures = []
with capture_index.open("r", encoding="utf-8", newline="") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        captures.append(
            {
                "level": int(row["level"]),
                "trace": row["trace"],
                "log": row["log"],
                "screenshot": row["screenshot"],
                "screenshot_health": load_json(row["screenshot_json"]),
                "render_health": load_json(row["render_json"]),
            }
        )

failures = []
for capture in captures:
    if not capture["screenshot_health"].get("ok"):
        failures.append(f"level {capture['level']}: screenshot health failed")
    if capture["render_health"].get("status") != "pass":
        failures.append(f"level {capture['level']}: render health failed")

summary = {
    "status": "fail" if failures else "pass",
    "capture_count": len(captures),
    "intro_summary_json": str(intro_summary_json),
    "failures": failures,
    "captures": captures,
}
with capture_summary_json.open("w", encoding="utf-8") as handle:
    json.dump(summary, handle, indent=2, sort_keys=True)
    handle.write("\n")
print(f"capture_summary_json: {capture_summary_json}")
if failures:
    for failure in failures:
        print(f"  {failure}")
    raise SystemExit(1)
PY

echo ""
echo "=== Intro Census Capture: PASS ==="
echo "  summary: $OUT_DIR/summary.txt"
echo "  json:    $OUT_DIR/summary.json"
echo "  capture: $CAPTURE_SUMMARY_JSON"
