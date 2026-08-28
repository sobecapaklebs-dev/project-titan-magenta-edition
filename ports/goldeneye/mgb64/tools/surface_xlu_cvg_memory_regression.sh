#!/bin/bash
#
# surface_xlu_cvg_memory_regression.sh -- Guard Surface fog/tree RDP coverage-memory promotion.
#
# Captures are generated from the user's ROM and must stay local. Do not commit
# screenshots, traces, logs, or generated summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"
OUT_DIR="/tmp/mgb64_surface_xlu_cvg_memory_$$"
LEVEL=36
FRAME=360
MSAA_VALUES="0 4"
MIN_CHANGED_PCT="0.25"
MIN_PROMOTED_ROWS=100
MIN_UNPROMOTED_ROWS=100
MAX_DEFAULT_UNPROMOTED_ROWS=0
REPORT_LABEL="${GE007_XLU_CVG_REGRESSION_LABEL:-Surface XLU coverage-memory}"
REPORT_SLUG="${GE007_XLU_CVG_REGRESSION_SLUG:-surface_xlu_cvg_memory}"

usage() {
    cat <<'USAGE'
Usage: tools/surface_xlu_cvg_memory_regression.sh [options]

Options:
  --out-dir DIR              output directory (default: /tmp/...)
  --rom PATH                 ROM path (default: ./baserom.u.z64)
  --binary PATH              native binary path (default: build/ge007)
  --build-dir DIR            CMake build directory (default: build)
  --no-build                 reuse an existing native binary
  --timeout SECONDS          per-capture timeout (default: 90)
  --level LEVELID            level id (default: 36, Surface 1)
  --frame N                  screenshot/trace frame (default: 360)
  --msaa-values LIST         quoted list of MSAA values (default: "0 4")
  --min-changed-pct F        min screenshot delta default vs disabled (default: 0.25)
  --min-promoted-rows N      min default alpha_rdp_cvg_memory rows (default: 100)
  --min-unpromoted-rows N    min disabled unpromoted candidate rows (default: 100)
  --max-default-unpromoted-rows N
                             max default unpromoted candidate rows (default: 0)

Artifacts are ROM-derived local validation data. Do not commit or redistribute
captured screenshots or traces.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --frame) FRAME="$2"; shift 2 ;;
        --msaa-values) MSAA_VALUES="$2"; shift 2 ;;
        --min-changed-pct) MIN_CHANGED_PCT="$2"; shift 2 ;;
        --min-promoted-rows) MIN_PROMOTED_ROWS="$2"; shift 2 ;;
        --min-unpromoted-rows) MIN_UNPROMOTED_ROWS="$2"; shift 2 ;;
        --max-default-unpromoted-rows) MAX_DEFAULT_UNPROMOTED_ROWS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$FRAME" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frame must be a positive integer: $FRAME" >&2
    exit 2
fi
if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi
if [[ ! "$LEVEL" =~ ^-?[0-9]+$ ]]; then
    echo "FAIL: --level must be an integer LEVELID: $LEVEL" >&2
    exit 2
fi
if [[ ! "$MIN_PROMOTED_ROWS" =~ ^[1-9][0-9]*$ || ! "$MIN_UNPROMOTED_ROWS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: min row thresholds must be positive integers" >&2
    exit 2
fi
if [[ ! "$MAX_DEFAULT_UNPROMOTED_ROWS" =~ ^[0-9]+$ ]]; then
    echo "FAIL: --max-default-unpromoted-rows must be a non-negative integer" >&2
    exit 2
fi
if [[ -z "${MSAA_VALUES//[[:space:]]/}" ]]; then
    echo "FAIL: --msaa-values list must not be empty" >&2
    exit 2
fi
for msaa in $MSAA_VALUES; do
    if [[ ! "$msaa" =~ ^[0-9]+$ ]]; then
        echo "FAIL: --msaa-values contains a non-integer value: $msaa" >&2
        exit 2
    fi
done
if ! python3 - "$MIN_CHANGED_PCT" <<'PY'
import math
import sys

try:
    value = float(sys.argv[1])
except ValueError:
    raise SystemExit(1)
raise SystemExit(0 if math.isfinite(value) and value >= 0.0 else 1)
PY
then
    echo "FAIL: --min-changed-pct must be a non-negative finite number: $MIN_CHANGED_PCT" >&2
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
SUMMARY_JSON="$OUT_DIR/summary.json"
TRACE_AFTER=$((FRAME > 20 ? FRAME - 20 : 0))

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

capture_variant() {
    local msaa="$1"
    local variant="$2"
    local case_dir="$OUT_DIR/msaa_${msaa}/${variant}"
    local label="${REPORT_SLUG}_${variant}_msaa${msaa}_$$"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local screenshot_src="$case_dir/screenshot_${label}.bmp"
    local screenshot_dst="$case_dir/screenshot.bmp"
    local render_json="$case_dir/render.json"
    local screenshot_json="$case_dir/screenshot_health.json"
    local rdp_json="$case_dir/rdp_modes.json"
    local toggle_env=()
    local cmd=()

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$screenshot_src" "$screenshot_dst" "$render_json" "$screenshot_json" "$rdp_json"

    if [[ "$variant" == "default" ]]; then
        toggle_env=("GE007_ROOM_XLU_CVG_MEMORY=1")
    else
        toggle_env=("GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1")
    fi

    cmd=(env -u GE007_DEBUG
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
        GE007_MUTE=1
        GE007_DETERMINISTIC_STABLE_COUNT=1
        GE007_NO_VSYNC=1
        GE007_BACKGROUND=1
        GE007_NO_INPUT_GRAB=1
        GE007_TRACE_RDP_RENDER_MODES=1
        GE007_TRACE_RDP_RENDER_MODES_AFTER_FRAME="$TRACE_AFTER"
        GE007_TRACE_RDP_RENDER_MODES_BUDGET=1600
        GE007_TRACE_RDP_RENDER_MODES_DRAWCLASS=room
        "${toggle_env[@]}"
        "$BINARY"
        --savedir "$case_dir"
        --rom "$ROM"
        --level "$LEVEL"
        --deterministic
        --trace-state "$trace"
        --screenshot-frame "$FRAME"
        --screenshot-label "$label"
        --screenshot-exit
        --config-override Video.WindowWidth=640
        --config-override Video.WindowHeight=480
        --config-override Video.WindowX=-1
        --config-override Video.WindowY=-1
        --config-override Video.WindowMode=windowed
        --config-override Video.RenderScale=1
        --config-override Video.MSAA="$msaa")

    echo "  capture msaa=${msaa} ${variant}"
    if ! (
        cd "$case_dir"
        if [[ -n "$TIMEOUT_BIN" ]]; then
            "$TIMEOUT_BIN" --kill-after=5 "$TIMEOUT_SECONDS" "${cmd[@]}"
        else
            "${cmd[@]}"
        fi
    ) >"$log" 2>&1; then
        echo "FAIL: capture failed for msaa=${msaa} ${variant}"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi

    if [[ ! -s "$screenshot_src" ]]; then
        echo "FAIL: missing screenshot for msaa=${msaa} ${variant}: $screenshot_src" >&2
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi
    mv "$screenshot_src" "$screenshot_dst"

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during msaa=${msaa} ${variant}"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /'
        exit 1
    fi

    python3 tools/audit_screenshot_health.py \
        --label "${REPORT_SLUG}/${variant}/msaa${msaa}" \
        --expect-size 640x480 \
        --json-out "$screenshot_json" \
        "$screenshot_dst" >/dev/null
    python3 tools/audit_render_trace.py \
        --label "${REPORT_SLUG}/${variant}/msaa${msaa}" \
        --json-out "$render_json" \
        "$trace" >/dev/null
    python3 tools/summarize_rdp_render_mode_trace.py \
        "$log" \
        --top 20 \
        --json-out "$rdp_json" >/dev/null
}

validate_pair() {
    local msaa="$1"
    local base="$OUT_DIR/msaa_${msaa}"
    local compare_json="$base/default_vs_disabled_screenshot.json"
    local compare_log="$base/default_vs_disabled_screenshot.txt"
    local state_log="$base/default_vs_disabled_state.txt"

    python3 tools/compare_screenshots.py \
        "$base/default/screenshot.bmp" \
        "$base/disabled/screenshot.bmp" \
        --json-out "$compare_json" \
        >"$compare_log"
    python3 tools/compare_state.py \
        "$base/default/state.jsonl" \
        "$base/disabled/state.jsonl" \
        >"$state_log"

    python3 - \
        "$msaa" \
        "$MIN_CHANGED_PCT" \
        "$MIN_PROMOTED_ROWS" \
        "$MIN_UNPROMOTED_ROWS" \
        "$MAX_DEFAULT_UNPROMOTED_ROWS" \
        "$base/default/rdp_modes.json" \
        "$base/disabled/rdp_modes.json" \
        "$compare_json" \
        "$REPORT_LABEL" <<'PY'
import json
import sys

msaa = sys.argv[1]
min_changed = float(sys.argv[2])
min_promoted = int(sys.argv[3])
min_unpromoted = int(sys.argv[4])
max_default_unpromoted = int(sys.argv[5])
with open(sys.argv[6], "r", encoding="utf-8") as handle:
    default = json.load(handle)
with open(sys.argv[7], "r", encoding="utf-8") as handle:
    disabled = json.load(handle)
with open(sys.argv[8], "r", encoding="utf-8") as handle:
    compare = json.load(handle)
report_label = sys.argv[9]

failures = []
promoted = int(default.get("promoted_coverage_memory_rows", 0))
default_unpromoted = int(default.get("unpromoted_coverage_candidate_rows", 0))
disabled_promoted = int(disabled.get("promoted_coverage_memory_rows", 0))
disabled_unpromoted = int(disabled.get("unpromoted_coverage_candidate_rows", 0))
changed_pct = float(compare.get("changed_pct", 0.0))

if promoted < min_promoted:
    failures.append(f"default promoted rows {promoted} < {min_promoted}")
if default_unpromoted > max_default_unpromoted:
    failures.append(
        f"default left {default_unpromoted} unpromoted coverage candidates "
        f"> {max_default_unpromoted}"
    )
if disabled_promoted != 0:
    failures.append(f"disabled run still promoted {disabled_promoted} coverage-memory rows")
if disabled_unpromoted < min_unpromoted:
    failures.append(f"disabled unpromoted candidates {disabled_unpromoted} < {min_unpromoted}")
if changed_pct < min_changed:
    failures.append(f"screenshot delta {changed_pct:.3f}% < {min_changed:.3f}%")

if failures:
    print(f"FAIL: {report_label} msaa={msaa}")
    for failure in failures:
        print(f"  {failure}")
    raise SystemExit(1)

print(
    f"PASS: {report_label} msaa={msaa} "
    f"promoted={promoted} disabled_candidates={disabled_unpromoted} "
    f"changed={changed_pct:.3f}%"
)
PY
}

echo "=== $REPORT_LABEL Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frame:   $FRAME"
echo "  msaa:    $MSAA_VALUES"

for msaa in $MSAA_VALUES; do
    capture_variant "$msaa" default
    capture_variant "$msaa" disabled
    validate_pair "$msaa"
done

python3 - "$OUT_DIR" "$SUMMARY_JSON" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
cases = []
for msaa_dir in sorted(root.glob("msaa_*")):
    msaa = msaa_dir.name.removeprefix("msaa_")
    with (msaa_dir / "default" / "rdp_modes.json").open("r", encoding="utf-8") as handle:
        default = json.load(handle)
    with (msaa_dir / "disabled" / "rdp_modes.json").open("r", encoding="utf-8") as handle:
        disabled = json.load(handle)
    with (msaa_dir / "default_vs_disabled_screenshot.json").open("r", encoding="utf-8") as handle:
        compare = json.load(handle)
    cases.append({
        "msaa": int(msaa),
        "default_promoted_coverage_memory_rows": default.get("promoted_coverage_memory_rows"),
        "default_unpromoted_coverage_candidate_rows": default.get("unpromoted_coverage_candidate_rows"),
        "disabled_promoted_coverage_memory_rows": disabled.get("promoted_coverage_memory_rows"),
        "disabled_unpromoted_coverage_candidate_rows": disabled.get("unpromoted_coverage_candidate_rows"),
        "changed_pct": compare.get("changed_pct"),
        "changed_pixels": compare.get("changed_pixels"),
    })

payload = {
    "status": "pass",
    "out_dir": str(root),
    "cases": cases,
}
summary_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
print(f"summary_json: {summary_path}")
PY
