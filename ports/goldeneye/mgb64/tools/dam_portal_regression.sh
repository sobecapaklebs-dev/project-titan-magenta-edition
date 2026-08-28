#!/bin/bash
#
# dam_portal_regression.sh -- Guard Dam pad-140 portal over-admission.
#
# The regression captures the same control-room/wall-contact view three ways:
#   1. default portal BFS
#   2. old broad native projection/widening behavior explicitly restored,
#      including the pre-ordering portal setup used by that legacy control
#   3. portal BFS disabled as a diagnostic broad-frustum comparison
#
# The default must stay tight and visually match the broad-frustum diagnostic at
# this frame, while the legacy bundle must still demonstrate the historical
# over-admission signature. Captures are ROM-derived local artifacts.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_dam_portal_regression_$$"
FRAMES=130

usage() {
    cat <<'USAGE'
Usage: tools/dam_portal_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 130)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
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
validation_acquire_runtime_lock

cleanup() {
    validation_release_runtime_lock
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

run_capture() {
    local name="$1"
    shift
    local case_dir="$OUT_DIR/$name"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local shot="$case_dir/screenshot_${name}.bmp"

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$shot" "$case_dir/render.json" "$case_dir/render.txt" \
        "$case_dir/screenshot.json" "$case_dir/screenshot.txt"

    echo "  capture: $name"
    if ! (
        cd "$case_dir"
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DEBUG=1 \
            GE007_ASSERT_ON_FAIL=0 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_AUTO_WARP_FRAME=40 \
            GE007_AUTO_WARP_PAD=140 \
            GE007_AUTO_FORWARD=70:80 \
            GE007_RENDER_CAMERA_EXTRA_CLEARANCE=6 \
            "$@" \
            "$BINARY" \
            --savedir "$case_dir" \
            --rom "$ROM" \
            --level 33 \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "$name" \
            --screenshot-exit
    ) >"$log" 2>&1; then
        echo "FAIL: capture failed for $name" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during $name" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for $name: $trace" >&2
        exit 1
    fi
    if [[ ! -s "$shot" ]]; then
        echo "FAIL: missing screenshot for $name: $shot" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    python3 tools/audit_render_trace.py \
        --label "dam portal $name" \
        --json-out "$case_dir/render.json" \
        "$trace" >"$case_dir/render.txt"
    python3 tools/audit_screenshot_health.py \
        --label "dam portal $name" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt"
}

echo "=== Dam Portal Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES"

run_capture default
run_capture old_fallback \
    GE007_BGORDER_PORTAL=0 \
    GE007_BG_PORTAL_AABB_EXPAND=0 \
    GE007_PORTAL_BACKFACE_PROJECT_FALLBACK=1 \
    GE007_PORTAL_LEGACY_PROJECT_CLAMP=1 \
    GE007_PORTAL_PARENT_CLIP_MIN_SPAN=8 \
    GE007_PORTAL_ACCEPTED_MIN_SPAN=24 \
    GE007_PORTAL_PROJECT_FRUSTUM_FALLBACK=0 \
    GE007_PORTAL_RETRY_SCREEN_CLIP=1
run_capture no_portal_bfs GE007_PORTAL_BFS=0

python3 tools/compare_screenshots.py \
    "$OUT_DIR/default/screenshot_default.bmp" \
    "$OUT_DIR/no_portal_bfs/screenshot_no_portal_bfs.bmp" \
    --max-changed-pct 0.10 \
    --json-out "$OUT_DIR/default_vs_no_portal_bfs.json" \
    >"$OUT_DIR/default_vs_no_portal_bfs.txt"

python3 tools/compare_screenshots.py \
    "$OUT_DIR/default/screenshot_default.bmp" \
    "$OUT_DIR/old_fallback/screenshot_old_fallback.bmp" \
    --json-out "$OUT_DIR/default_vs_old_fallback.json" \
    >"$OUT_DIR/default_vs_old_fallback.txt"

python3 - "$OUT_DIR" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])

def load_last_trace(case):
    last = None
    trace = root / case / "state.jsonl"
    with trace.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                last = json.loads(line)
    if last is None:
        raise SystemExit(f"FAIL: no trace records for {case}")
    return last

def rendered_count(record):
    rooms = record.get("rooms", {})
    vis = rooms.get("vis", {})
    value = rooms.get("rendered_count", vis.get("rendered"))
    if value is None:
        raise SystemExit("FAIL: trace missing rendered room count")
    return int(value)

default = load_last_trace("default")
old = load_last_trace("old_fallback")
no_portal = load_last_trace("no_portal_bfs")

default_rooms = rendered_count(default)
old_rooms = rendered_count(old)
no_portal_rooms = rendered_count(no_portal)

default_vs_no = json.loads((root / "default_vs_no_portal_bfs.json").read_text())
default_vs_old = json.loads((root / "default_vs_old_fallback.json").read_text())

failures = []

if default.get("rooms", {}).get("cur") != 114:
    failures.append(f"default current room is not 114: {default.get('rooms', {}).get('cur')}")
if default_rooms > 25:
    failures.append(f"default rendered too many rooms: {default_rooms} > 25")
if old_rooms < 35:
    failures.append(f"legacy bundle did not reproduce over-admission: {old_rooms} < 35")
if old_rooms - default_rooms < 20:
    failures.append(
        f"legacy bundle room delta too small: old={old_rooms} default={default_rooms}"
    )
if no_portal_rooms < default_rooms:
    failures.append(
        f"no_portal_bfs rendered fewer rooms than default: {no_portal_rooms} < {default_rooms}"
    )

changed_no = float(default_vs_no.get("changed_pct", 100.0))
changed_old = float(default_vs_old.get("changed_pct", 0.0))

if changed_no > 0.10:
    failures.append(f"default vs no_portal_bfs changed {changed_no:.3f}% > 0.10%")
if changed_old < 5.0:
    failures.append(f"default vs old_fallback changed {changed_old:.3f}% < 5.0%")

summary = {
    "status": "fail" if failures else "pass",
    "default": {
        "frame": default.get("f"),
        "current_room": default.get("rooms", {}).get("cur"),
        "rendered_rooms": default_rooms,
        "sample": default.get("rooms", {}).get("vis", {}).get("sample"),
    },
    "old_fallback": {
        "frame": old.get("f"),
        "current_room": old.get("rooms", {}).get("cur"),
        "rendered_rooms": old_rooms,
        "sample": old.get("rooms", {}).get("vis", {}).get("sample"),
    },
    "no_portal_bfs": {
        "frame": no_portal.get("f"),
        "current_room": no_portal.get("rooms", {}).get("cur"),
        "rendered_rooms": no_portal_rooms,
        "sample": no_portal.get("rooms", {}).get("vis", {}).get("sample"),
    },
    "default_vs_no_portal_bfs_changed_pct": changed_no,
    "default_vs_old_fallback_changed_pct": changed_old,
    "failures": failures,
}

(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: Dam portal regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Dam portal regression")
print(
    "  rendered rooms: default=%d old_fallback=%d no_portal_bfs=%d"
    % (default_rooms, old_rooms, no_portal_rooms)
)
print(
    "  image changed: default_vs_no_portal_bfs=%.3f%% default_vs_old_fallback=%.3f%%"
    % (changed_no, changed_old)
)
PY

echo "summary_json: $OUT_DIR/summary.json"
echo "artifacts: $OUT_DIR"
