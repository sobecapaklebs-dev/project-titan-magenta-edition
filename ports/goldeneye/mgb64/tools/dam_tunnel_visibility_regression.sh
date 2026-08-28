#!/bin/bash
#
# dam_tunnel_visibility_regression.sh -- Guard Dam tunnel portal under-admission.
#
# This captures the Dam service tunnel at pad 164. The default portal path must
# render the visible continuation instead of exposing the blue sky/fog clear
# through the tunnel. `pre_ordering` disables the portal ordering and portal-AABB
# expansion fixes to reproduce the old under-admission with fewer rooms and a
# higher blue-cap signature than the default capture.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_dam_tunnel_visibility_$$"
FRAMES=120

usage() {
    cat <<'USAGE'
Usage: tools/dam_tunnel_visibility_regression.sh [options]

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
            GE007_AUTO_WARP_PAD=164 \
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

    python3 tools/audit_screenshot_health.py \
        --label "dam tunnel visibility $name" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt"
}

echo "=== Dam Tunnel Visibility Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES"

run_capture default
run_capture pre_ordering \
    GE007_BGORDER_PORTAL=0 \
    GE007_BG_PORTAL_AABB_EXPAND=0 \
    GE007_PORTAL_BACKFACE_PROJECT_FALLBACK=0
run_capture no_portal_bfs GE007_PORTAL_BFS=0

python3 - "$OUT_DIR" <<'PY'
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit(
        "FAIL: Pillow is required for Dam tunnel visibility metrics. "
        "Install it with: python3 -m pip install pillow"
    )

root = Path(sys.argv[1])

def last_trace(case):
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

def room_sample(record):
    return record.get("rooms", {}).get("vis", {}).get("sample", [])

def room_draw_sample(record):
    return record.get("rooms", {}).get("vis", {}).get("draw_sample", [])

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

records = {case: last_trace(case) for case in ("default", "pre_ordering", "no_portal_bfs")}
rooms = {case: rendered_count(record) for case, record in records.items()}
blue = {case: bright_blue_pct(case) for case in records}

failures = []

if rooms["default"] < 5:
    failures.append(f"default rendered too few tunnel rooms: {rooms['default']} < 5")
if 98 not in room_sample(records["default"]) and 98 not in room_draw_sample(records["default"]):
    failures.append(
        "default did not render room 98 continuation: "
        f"sample={room_sample(records['default'])} "
        f"draw_sample={room_draw_sample(records['default'])}"
    )
if blue["default"] > 0.50:
    failures.append(f"default bright-blue cap {blue['default']:.3f}% > 0.50%")
if rooms["pre_ordering"] >= rooms["default"]:
    failures.append(
        f"pre_ordering did not reproduce lower room count: "
        f"{rooms['pre_ordering']} >= {rooms['default']}"
    )
if blue["pre_ordering"] <= blue["default"] + 0.05:
    failures.append(
        f"pre_ordering blue-cap control too weak: "
        f"{blue['pre_ordering']:.3f}% <= default {blue['default']:.3f}% + 0.05%"
    )
if rooms["no_portal_bfs"] < rooms["default"]:
    failures.append(
        f"no_portal_bfs rendered fewer rooms than default: "
        f"{rooms['no_portal_bfs']} < {rooms['default']}"
    )

summary = {
    "status": "fail" if failures else "pass",
    "default": {
        "frame": records["default"].get("f"),
        "current_room": records["default"].get("rooms", {}).get("cur"),
        "rendered_rooms": rooms["default"],
        "sample": room_sample(records["default"]),
        "draw_sample": room_draw_sample(records["default"]),
        "bright_blue_pct": blue["default"],
    },
    "pre_ordering": {
        "frame": records["pre_ordering"].get("f"),
        "current_room": records["pre_ordering"].get("rooms", {}).get("cur"),
        "rendered_rooms": rooms["pre_ordering"],
        "sample": room_sample(records["pre_ordering"]),
        "draw_sample": room_draw_sample(records["pre_ordering"]),
        "bright_blue_pct": blue["pre_ordering"],
    },
    "no_portal_bfs": {
        "frame": records["no_portal_bfs"].get("f"),
        "current_room": records["no_portal_bfs"].get("rooms", {}).get("cur"),
        "rendered_rooms": rooms["no_portal_bfs"],
        "sample": room_sample(records["no_portal_bfs"]),
        "draw_sample": room_draw_sample(records["no_portal_bfs"]),
        "bright_blue_pct": blue["no_portal_bfs"],
    },
    "failures": failures,
}

(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: Dam tunnel visibility regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Dam tunnel visibility regression")
print(
    "  rendered rooms: default=%d pre_ordering=%d no_portal_bfs=%d"
    % (rooms["default"], rooms["pre_ordering"], rooms["no_portal_bfs"])
)
print(
    "  bright blue: default=%.3f%% pre_ordering=%.3f%% no_portal_bfs=%.3f%%"
    % (blue["default"], blue["pre_ordering"], blue["no_portal_bfs"])
)
PY

echo "summary_json: $OUT_DIR/summary.json"
echo "artifacts: $OUT_DIR"
