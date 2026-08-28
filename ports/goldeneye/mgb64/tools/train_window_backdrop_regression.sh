#!/bin/bash
#
# train_window_backdrop_regression.sh -- Guard Train rear-car backdrop leaks.
#
# This captures Train room 51 at pad 74, whose authored look direction faces the
# shuttered window beside the rear-car desk. Portal BFS can render only room 51
# there, leaving the sky/backdrop texture visible through slats that should be
# partially covered by the one-hop exterior/window neighbor rooms. The default
# portal-edge rescue must render that rejected adjacent room; disabling
# GE007_PORTAL_EDGE_RESCUE reproduces the leak control.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_train_window_backdrop_$$"
FRAMES=120
PYTHON_BIN="${PYTHON:-python3}"

usage() {
    cat <<'USAGE'
Usage: tools/train_window_backdrop_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 120)
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
    rm -f "$trace" "$log" "$shot" "$case_dir/screenshot.json" "$case_dir/screenshot.txt"

    echo "  capture: $name"
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
            GE007_DEBUG=1 \
            GE007_ASSERT_ON_FAIL=0 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_AUTO_WARP_FRAME=40 \
            GE007_AUTO_WARP_PAD=74 \
            "$@" \
            "$BINARY" \
            --savedir "$case_dir" \
            --rom "$ROM" \
            --level train \
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

    "$PYTHON_BIN" tools/audit_screenshot_health.py \
        --label "train window backdrop $name" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt"
}

echo "=== Train Window Backdrop Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES"

run_capture default
run_capture rescue_disabled GE007_PORTAL_EDGE_RESCUE=0

"$PYTHON_BIN" tools/compare_screenshots.py \
    --json-out "$OUT_DIR/default_vs_rescue_disabled.json" \
    "$OUT_DIR/default/screenshot_default.bmp" \
    "$OUT_DIR/rescue_disabled/screenshot_rescue_disabled.bmp" \
    >"$OUT_DIR/default_vs_rescue_disabled.txt"

"$PYTHON_BIN" - "$OUT_DIR" <<'PY'
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit(
        "FAIL: Pillow is required for Train window backdrop metrics. "
        "Install it with: python3 -m pip install pillow"
    )

root = Path(sys.argv[1])

def last_trace(case):
    last = None
    with (root / case / "state.jsonl").open("r", encoding="utf-8") as handle:
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

def room_sample(record):
    return record.get("rooms", {}).get("vis", {}).get("sample", [])

def bright_blue_pct(case):
    path = root / case / f"screenshot_{case}.bmp"
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        total = rgb.width * rgb.height
        data = rgb.tobytes()
        bright = 0
        for index in range(0, len(data), 3):
            r = data[index]
            g = data[index + 1]
            b = data[index + 2]
            if b > 80 and b > r + 24 and b > g + 12:
                bright += 1
    return 100.0 * float(bright) / float(total)

records = {case: last_trace(case) for case in ("default", "rescue_disabled")}
rooms = {case: rendered_count(record) for case, record in records.items()}
samples = {case: room_sample(record) for case, record in records.items()}
blue = {case: bright_blue_pct(case) for case in records}

compare = json.loads((root / "default_vs_rescue_disabled.json").read_text(encoding="utf-8"))
changed_pct = float(compare.get("changed_pct", 0.0))

failures = []

if rooms["default"] < 2:
    failures.append(f"default rendered too few Train rear-window rooms: {rooms['default']} < 2")
if 53 not in samples["default"]:
    failures.append(f"default sample missing visible adjacent room 53: {samples['default']}")
if rooms["rescue_disabled"] >= rooms["default"]:
    failures.append(
        f"rescue-disabled control did not lower room count: "
        f"{rooms['rescue_disabled']} >= {rooms['default']}"
    )
if blue["default"] + 0.20 >= blue["rescue_disabled"]:
    failures.append(
        f"default sky-blue window leak too close to disabled control: "
        f"{blue['default']:.3f}% + 0.20 >= {blue['rescue_disabled']:.3f}%"
    )
if changed_pct < 5.0:
    failures.append(
        f"default vs rescue-disabled visual delta too small: {changed_pct:.3f}% < 5.0%"
    )

summary = {
    "status": "fail" if failures else "pass",
    "default": {
        "frame": records["default"].get("f"),
        "current_room": records["default"].get("rooms", {}).get("cur"),
        "rendered_rooms": rooms["default"],
        "sample": samples["default"],
        "bright_blue_pct": blue["default"],
    },
    "rescue_disabled": {
        "frame": records["rescue_disabled"].get("f"),
        "current_room": records["rescue_disabled"].get("rooms", {}).get("cur"),
        "rendered_rooms": rooms["rescue_disabled"],
        "sample": samples["rescue_disabled"],
        "bright_blue_pct": blue["rescue_disabled"],
    },
    "changed_pct": changed_pct,
    "failures": failures,
}

(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: Train window backdrop regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Train window backdrop regression")
print(
    "  default: rooms=%d sample=%s bright_blue=%.3f%%"
    % (rooms["default"], samples["default"], blue["default"])
)
print(
    "  rescue_disabled: rooms=%d sample=%s bright_blue=%.3f%%"
    % (rooms["rescue_disabled"], samples["rescue_disabled"], blue["rescue_disabled"])
)
print("  changed_pct=%.3f%%" % changed_pct)
print("  artifacts: %s" % root)
PY
