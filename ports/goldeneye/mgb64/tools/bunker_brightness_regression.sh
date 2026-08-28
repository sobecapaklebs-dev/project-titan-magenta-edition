#!/bin/bash
#
# bunker_brightness_regression.sh -- Guard Bunker 1 faithful brightness health.
#
# Bunker is an intentionally dark level, so this is not a stock pixel oracle.
# It pins a faithful baseline in an isolated savedir, verifies the capture is
# not blank/pathologically black, checks render-health and room/tris coverage,
# then runs a bright remaster A/B to prove the measurement is sensitive to
# post-processing brightness changes.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_bunker_brightness_$$"
LEVEL=9
FRAMES=120
MIN_DEFAULT_CENTER_LUMA=35.0
MAX_DEFAULT_CENTER_LUMA=120.0
MAX_DEFAULT_CENTER_BLACK_PCT=5.0
MIN_DEFAULT_ROOMS=5
MIN_DEFAULT_TRIS=1000
MIN_BRIGHT_CENTER_LUMA_DELTA=5.0

usage() {
    cat <<'USAGE'
Usage: tools/bunker_brightness_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 120)
  --level N            raw LEVELID (default: 9, Bunker 1)
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
        --level) LEVEL="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for pair in \
    "frames:$FRAMES" \
    "level:$LEVEL" \
    "timeout:$TIMEOUT_SECONDS"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: --$name must be a positive integer: $value" >&2
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
validation_acquire_runtime_lock

cleanup() {
    validation_release_runtime_lock
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

BASE_CONFIG_OVERRIDES=(
    "Input.SteadyView=1"
    "Video.WindowWidth=640"
    "Video.WindowHeight=480"
    "Video.WindowX=-1"
    "Video.WindowY=-1"
    "Video.WindowMode=windowed"
    "Video.HiDPI=0"
    "Video.RenderScale=1"
    "Video.MSAA=0"
    "Video.FovY=60"
    "Video.ViewmodelFov=60"
    "Video.RetroFilter=off"
)

FAITHFUL_OVERRIDES=(
    "Video.RemasterFX=0"
    "Video.GradePresets=0"
    "Video.Tonemap=0"
    "Video.Bloom=0"
    "Video.Vignette=0"
    "Video.Saturation=1"
    "Video.Contrast=1"
    "Video.Brightness=0"
)

BRIGHT_OVERRIDES=(
    "Video.RemasterFX=1"
    "Video.GradePresets=1"
    "Video.Tonemap=1"
    "Video.Bloom=1"
    "Video.Vignette=0"
    "Video.Saturation=1.15"
    "Video.Contrast=1.08"
    "Video.Brightness=0.18"
)

echo "=== Bunker Brightness Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL"
echo "  frames:  $FRAMES"

run_capture() {
    local name="$1"
    shift
    local case_dir="$OUT_DIR/$name"
    local save_dir="$case_dir/save"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local shot="$case_dir/screenshot_${name}.bmp"
    local config_args=()
    local override

    rm -rf "$case_dir"
    mkdir -p "$save_dir"

    for override in "${BASE_CONFIG_OVERRIDES[@]}" "$@"; do
        config_args+=(--config-override "$override")
    done

    echo "  capture: $name"
    if ! (
        cd "$case_dir"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
                SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
                GE007_MUTE=1 \
                GE007_DETERMINISTIC_STABLE_COUNT=1 \
                GE007_NO_VSYNC=1 \
                GE007_BACKGROUND=1 \
                GE007_NO_INPUT_GRAB=1 \
                GE007_FIDELITY_CAPTURE=1 \
                GE007_DISABLE_LEVEL_INTRO=1 \
                "$BINARY" \
                --savedir "$save_dir" \
                --rom "$ROM" \
                --level "$LEVEL" \
                --deterministic \
                --trace-state "$trace" \
                --screenshot-frame "$FRAMES" \
                --screenshot-label "$name" \
                --screenshot-exit \
                "${config_args[@]}"
    ) >"$log" 2>&1; then
        echo "FAIL: Bunker brightness capture failed for $name" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during $name" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if grep -qF "[GFX-DL]" "$log"; then
        echo "FAIL: GFX-DL diagnostic rows observed during $name" >&2
        grep -F "[GFX-DL]" "$log" | head -20 | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for $name: $trace" >&2
        exit 1
    fi
    if [[ ! -s "$shot" ]]; then
        echo "FAIL: missing screenshot for $name: $shot" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    python3 tools/audit_screenshot_health.py \
        --label "Bunker brightness $name" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt"
}

run_capture default "${FAITHFUL_OVERRIDES[@]}"
run_capture bright_ab "${BRIGHT_OVERRIDES[@]}"

python3 - "$OUT_DIR" \
    "$LEVEL" \
    "$MIN_DEFAULT_CENTER_LUMA" \
    "$MAX_DEFAULT_CENTER_LUMA" \
    "$MAX_DEFAULT_CENTER_BLACK_PCT" \
    "$MIN_DEFAULT_ROOMS" \
    "$MIN_DEFAULT_TRIS" \
    "$MIN_BRIGHT_CENTER_LUMA_DELTA" <<'PY'
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit(
        "FAIL: Pillow is required for Bunker brightness metrics. "
        "Install it with: python3 -m pip install pillow"
    )

root = Path(sys.argv[1])
expected_level = int(sys.argv[2])
min_default_center_luma = float(sys.argv[3])
max_default_center_luma = float(sys.argv[4])
max_default_center_black_pct = float(sys.argv[5])
min_default_rooms = int(sys.argv[6])
min_default_tris = int(sys.argv[7])
min_bright_center_luma_delta = float(sys.argv[8])
cases = ("default", "bright_ab")
failures = []

def image_stats(case: str) -> dict:
    path = root / case / f"screenshot_{case}.bmp"
    if not path.exists():
        failures.append(f"{case}: missing screenshot {path}")
        return {
            "path": str(path),
            "full_luma": 0.0,
            "center_luma": 0.0,
            "center_black_pct": 100.0,
            "logical_luma": 0.0,
        }
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        boxes = {
            "full": (0, 0, rgb.width, rgb.height),
            "center": (rgb.width // 4, rgb.height // 4, rgb.width * 3 // 4, rgb.height * 3 // 4),
            "logical": (0, 20, rgb.width, max(20, rgb.height - 20)),
        }
        stats = {"path": str(path)}
        for label, box in boxes.items():
            crop = rgb.crop(box)
            pixels = list(crop.getdata())
            total = len(pixels)
            luma = sum(0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in pixels) / float(total)
            black = sum(1 for r, g, b in pixels if r <= 5 and g <= 5 and b <= 5)
            stats[f"{label}_luma"] = luma
            stats[f"{label}_black_pct"] = 100.0 * black / float(total)
        return stats

def last_trace(case: str) -> dict:
    path = root / case / "state.jsonl"
    last = None
    if not path.exists():
        failures.append(f"{case}: missing trace {path}")
        return {}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        raw = raw.strip()
        if raw:
            last = json.loads(raw)
    if last is None:
        failures.append(f"{case}: empty trace {path}")
        return {}
    return last

images = {case: image_stats(case) for case in cases}
traces = {case: last_trace(case) for case in cases}

default = images["default"]
bright = images["bright_ab"]
default_trace = traces["default"]

front = default_trace.get("front", {})
rooms = default_trace.get("rooms", {})
vis = rooms.get("vis", {}) if isinstance(rooms, dict) else {}
dl = default_trace.get("dl", {})

if int(front.get("active_stage", -1)) != expected_level:
    failures.append(f"default active_stage {front.get('active_stage')} != {expected_level}")
if int(front.get("loaded_stage", -1)) != expected_level:
    failures.append(f"default loaded_stage {front.get('loaded_stage')} != {expected_level}")
if int(vis.get("rendered", 0)) < min_default_rooms:
    failures.append(f"default rendered rooms {vis.get('rendered')} < {min_default_rooms}")
if int(default_trace.get("tris", 0)) < min_default_tris:
    failures.append(f"default tris {default_trace.get('tris')} < {min_default_tris}")
if int(default_trace.get("bad_cmds", 0)) != 0:
    failures.append(f"default bad_cmds {default_trace.get('bad_cmds')} != 0")
if int(default_trace.get("crashes", 0)) != 0:
    failures.append(f"default crashes {default_trace.get('crashes')} != 0")
if int(default_trace.get("nan", 0)) != 0:
    failures.append(f"default nan {default_trace.get('nan')} != 0")
for key, value in dl.items():
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        failures.append(f"default dl.{key} is non-integer: {value}")
        continue
    if parsed != 0:
        failures.append(f"default dl.{key} {parsed} != 0")
if default["center_luma"] < min_default_center_luma:
    failures.append(
        f"default center luma {default['center_luma']:.2f} < {min_default_center_luma:.2f}"
    )
if default["center_luma"] > max_default_center_luma:
    failures.append(
        f"default center luma {default['center_luma']:.2f} > {max_default_center_luma:.2f}"
    )
if default["center_black_pct"] > max_default_center_black_pct:
    failures.append(
        f"default center black {default['center_black_pct']:.2f}% > "
        f"{max_default_center_black_pct:.2f}%"
    )

center_delta = bright["center_luma"] - default["center_luma"]
if center_delta < min_bright_center_luma_delta:
    failures.append(
        f"bright_ab center luma delta {center_delta:.2f} < "
        f"{min_bright_center_luma_delta:.2f}"
    )

summary = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "level": expected_level,
    "stats": images,
    "default_trace": {
        "active_stage": front.get("active_stage"),
        "loaded_stage": front.get("loaded_stage"),
        "rooms_rendered": vis.get("rendered"),
        "rooms_sample": vis.get("sample"),
        "rooms_draw_sample": vis.get("draw_sample"),
        "tris": default_trace.get("tris"),
        "dl": dl,
        "bad_cmds": default_trace.get("bad_cmds"),
        "crashes": default_trace.get("crashes"),
        "nan": default_trace.get("nan"),
    },
    "thresholds": {
        "min_default_center_luma": min_default_center_luma,
        "max_default_center_luma": max_default_center_luma,
        "max_default_center_black_pct": max_default_center_black_pct,
        "min_default_rooms": min_default_rooms,
        "min_default_tris": min_default_tris,
        "min_bright_center_luma_delta": min_bright_center_luma_delta,
    },
}
(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

if failures:
    print("FAIL: Bunker brightness regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Bunker brightness regression")
print(
    "  default: full_luma={:.2f} center_luma={:.2f} center_black={:.2f}% rooms={} tris={}".format(
        default["full_luma"],
        default["center_luma"],
        default["center_black_pct"],
        vis.get("rendered"),
        default_trace.get("tris"),
    )
)
print(
    "  bright_ab: center_luma={:.2f} delta={:.2f}".format(
        bright["center_luma"],
        center_delta,
    )
)
print(f"summary_json: {root / 'summary.json'}")
print(f"artifacts: {root}")
PY
