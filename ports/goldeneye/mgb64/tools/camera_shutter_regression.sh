#!/bin/bash
#
# camera_shutter_regression.sh -- guard the camera shutter fade/state path.
#
# The camera item briefly fades the screen to black when fired, then fades back
# out and returns to normal weapon handling. A C-port drift grouped ITEM_CAMERA
# with regular guns, making the fade-out branch unreachable and leaving the
# screen black. This smoke equips/fires the camera on Silo, then requests a
# normal weapon switch and audits the trace state.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_camera_shutter_$$"
LEVEL="silo"
CAMERA_ITEM=40
ADD_FRAME=70
EQUIP_FRAME=80
FIRE_SPEC="132:4"
SWITCH_SPEC="180:4"
FRAMES=240

usage() {
    cat <<'USAGE'
Usage: tools/camera_shutter_regression.sh [options]

Options:
  --level LEVEL        level slug or raw LEVELID (default: silo)
  --frames N          deterministic exit frame (default: 240)
  --out-dir DIR       output dir (default: /tmp/...)
  --rom PATH          ROM path (default: ./baserom.u.z64)
  --binary PATH       native binary (default: build/ge007)
  --build-dir DIR     CMake build dir (default: build)
  --no-build          reuse an existing native binary
  --timeout SECONDS   process timeout (default: 120)

Artifacts are ROM-derived local validation data; do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="$2"; shift 2 ;;
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

TRACE="$OUT_DIR/trace.jsonl"
LOG="$OUT_DIR/run.log"

echo "== camera shutter: level=$LEVEL equip=$CAMERA_ITEM fire=$FIRE_SPEC switch=$SWITCH_SPEC =="

GAME_EXIT=0
(
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
            GE007_AUTO_ADD_ITEM_FRAME="$ADD_FRAME" \
            GE007_AUTO_ADD_ITEM="$CAMERA_ITEM" \
            GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_FRAME" \
            GE007_AUTO_EQUIP_ITEM="$CAMERA_ITEM" \
            GE007_AUTO_FIRE="$FIRE_SPEC" \
            GE007_AUTO_WEAPON_NEXT="$SWITCH_SPEC" \
            GE007_AUTO_EXIT_FRAME="$FRAMES" \
            "$BINARY" \
            --rom "$ROM" \
            --level "$LEVEL" \
            --difficulty agent \
            --deterministic \
            --trace-state "$TRACE" \
            --savedir "$OUT_DIR"
) >"$LOG" 2>&1 || GAME_EXIT=$?

if [[ "$GAME_EXIT" -eq 124 ]]; then
    echo "  process: FAIL (timeout)"
    tail -20 "$LOG" | sed 's/^/    /'
    exit 1
elif [[ "$GAME_EXIT" -ne 0 ]]; then
    echo "  process: FAIL (exit $GAME_EXIT)"
    tail -20 "$LOG" | sed 's/^/    /'
    exit 1
fi
echo "  process: PASS"

ASSERTS="$(grep -cF "[GEASSERT]" "$LOG" 2>/dev/null || true)"
if [[ "${ASSERTS:-0}" -ne 0 ]]; then
    echo "  assertions: FAIL ($ASSERTS)"
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

python3 - "$TRACE" "$CAMERA_ITEM" <<'PY'
import json
import sys
from pathlib import Path

trace = Path(sys.argv[1])
camera_item = int(sys.argv[2])

records = []
for raw in trace.read_text(encoding="utf-8").splitlines():
    if not raw.strip():
        continue
    rec = json.loads(raw)
    fade = rec.get("combat", {}).get("health", {}).get("fade_rgba", [255, 255, 255, 0.0])
    hands = rec.get("watch", {}).get("hands", {})
    active = hands.get("active", [-1, -1])
    next_items = hands.get("next", [-1, -1])
    records.append({
        "frame": int(rec.get("f", -1)),
        "fade": float(fade[3]) if isinstance(fade, list) and len(fade) >= 4 else 0.0,
        "fade_rgb": fade[:3] if isinstance(fade, list) and len(fade) >= 3 else [],
        "right_active": int(active[1]) if isinstance(active, list) and len(active) >= 2 else -1,
        "right_next": int(next_items[1]) if isinstance(next_items, list) and len(next_items) >= 2 else -1,
        "wr_state": int(rec.get("wr_state", -1)),
        "wr_item": int(rec.get("wr_item", -1)),
        "wr_valid": int(rec.get("wr_valid", 0)),
    })

failures = []
black = [r for r in records if r["frame"] >= 120 and r["fade"] >= 0.99 and r["fade_rgb"] == [0, 0, 0]]
if not black:
    failures.append("camera fire never reached an opaque black shutter fade")
else:
    first_black = black[0]["frame"]
    cleared = [r for r in records if r["frame"] > first_black and r["fade"] <= 0.001]
    if not cleared:
        failures.append("camera shutter fade never cleared back to transparent")
    else:
        first_clear = cleared[0]["frame"]
        if first_clear - first_black > 20:
            failures.append(f"camera shutter fade cleared too slowly: {first_clear - first_black} frames")

post_camera_idle = [
    r for r in records
    if r["frame"] >= 150 and r["frame"] < 180
    and r["right_active"] == camera_item
    and r["wr_item"] == camera_item
    and r["wr_state"] == 0
    and r["fade"] <= 0.001
]
if not post_camera_idle:
    failures.append("camera did not return to idle with fade cleared before weapon switch")

switched = [
    r for r in records
    if r["frame"] >= 200
    and r["right_active"] != camera_item
    and r["wr_item"] != camera_item
    and r["wr_state"] == 0
    and r["wr_valid"] == 1
    and r["fade"] <= 0.001
]
if not switched:
    failures.append("weapon-next did not switch away from camera after shutter fade")

if failures:
    print("  trace_audit: FAIL")
    for failure in failures:
        print(f"    {failure}")
    raise SystemExit(1)

print("  trace_audit: PASS")
print(
    "    black_frame=%d clear_frame=%d switched_frame=%d"
    % (black[0]["frame"], cleared[0]["frame"], switched[0]["frame"])
)
PY

echo "camera shutter regression: PASS"
echo "artifacts: $OUT_DIR"
