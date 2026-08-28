#!/bin/bash
#
# knife_throw_sfx_smoke.sh -- regression for throwing-knife flight SFX crashes.
#
# This covers the Bunker route where sub_GAME_7F043650 selects a random knife
# whoosh SFX while the projectile is in flight. A native aliasing bug once left
# part of that local SFX table uninitialized, sending garbage IDs into sndPlaySfx.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=180
OUT_DIR="/tmp/mgb64_knife_throw_sfx_$$"
LEVEL=9
CHRNUM=0
WARP_FRAME=60
WARP_DISTANCE=120
GIVE_FRAME=70
EQUIP_FRAME=85
FIRE_SPEC="110:3450"
FRAMES=3600

usage() {
    cat <<'USAGE'
Usage: tools/knife_throw_sfx_smoke.sh [options]

Options:
  --level N            raw LEVELID (default: 9 = Bunker 1)
  --chrnum C           guard to warp in front of Bond (default: 0)
  --warp-frame N       frame to warp the guard (default: 60)
  --warp-distance D    warp distance in front of Bond (default: 120)
  --give-frame N       frame to add knife + ammo (default: 70)
  --equip-frame N      frame to equip the throwing knife (default: 85)
  --fire-spec F:L      AUTO_FIRE window frame:len (default: 110:3450)
  --frames N           capture/exit frame (default: 3600)
  --out-dir DIR        output dir (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary (default: build/ge007)
  --build-dir DIR      CMake build dir (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    process timeout (default: 180)
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="$2"; shift 2 ;;
        --chrnum) CHRNUM="$2"; shift 2 ;;
        --warp-frame) WARP_FRAME="$2"; shift 2 ;;
        --warp-distance) WARP_DISTANCE="$2"; shift 2 ;;
        --give-frame) GIVE_FRAME="$2"; shift 2 ;;
        --equip-frame) EQUIP_FRAME="$2"; shift 2 ;;
        --fire-spec) FIRE_SPEC="$2"; shift 2 ;;
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
SFX_TRACE="$OUT_DIR/sfx.jsonl"
LOG="$OUT_DIR/run.log"

echo "== knife throw SFX: level $LEVEL, equip knife @$EQUIP_FRAME, fire $FIRE_SPEC, exit @$FRAMES =="

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
            GE007_TRACE_CHRNUM="$CHRNUM" \
            GE007_AUTO_WARP_CHRNUM="$CHRNUM" \
            GE007_AUTO_WARP_CHR_FRAME="$WARP_FRAME" \
            GE007_AUTO_WARP_CHR_DISTANCE="$WARP_DISTANCE" \
            GE007_AUTO_WARP_CHR_ANGLE=0 \
            GE007_AUTO_ADD_ITEM=3 \
            GE007_AUTO_ADD_ITEM_FRAME="$GIVE_FRAME" \
            GE007_AUTO_ADD_WEAPON_AMMO=3 \
            GE007_AUTO_ADD_WEAPON_AMMO_AMOUNT=50 \
            GE007_AUTO_ADD_WEAPON_AMMO_FRAME="$GIVE_FRAME" \
            GE007_AUTO_EQUIP_ITEM=3 \
            GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_FRAME" \
            GE007_AUTO_FIRE="$FIRE_SPEC" \
            GE007_SFX_TRACE_JSONL="$SFX_TRACE" \
            "$BINARY" \
            --rom "$ROM" \
            --level "$LEVEL" \
            --deterministic \
            --trace-state "$TRACE" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "knife_throw_sfx_$$" \
            --screenshot-exit
) >"$LOG" 2>&1 || GAME_EXIT=$?

if [[ "$GAME_EXIT" -eq 124 ]]; then
    echo "  process: FAIL (timeout)"
    tail -20 "$LOG" | sed 's/^/    /'
    exit 1
elif [[ "$GAME_EXIT" -ne 0 ]]; then
    echo "  process: FAIL (exit $GAME_EXIT -- throwing-knife SFX crash?)"
    grep -iE 'crash|signal|segmentation|fault|abort' "$LOG" | head -8 | sed 's/^/    /'
    tail -10 "$LOG" | sed 's/^/    /'
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

python3 - "$SFX_TRACE" <<'PY'
import json
import sys

path = sys.argv[1]
knife_sounds = {95, 96, 97}
seen = []
flight_submits = []

try:
    handle = open(path, encoding="utf-8")
except OSError as exc:
    print(f"  sfx trace: FAIL ({exc})")
    sys.exit(1)

with handle:
    for line in handle:
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        caller = rec.get("caller") or ""
        public_sound = rec.get("public_sound")
        line_no = rec.get("line")
        if (caller.endswith("chrobjhandler.c") and isinstance(line_no, int)
                and 6900 <= line_no <= 7200):
            item = (rec.get("frame"), public_sound, rec.get("bank_sound"), line_no)
            flight_submits.append(item)
            if public_sound in knife_sounds:
                seen.append(item)

if not seen:
    print("  knife SFX: FAIL (no throwing-knife whoosh submissions)")
    sys.exit(1)

bad = [item for item in flight_submits if item[1] not in knife_sounds]
if bad:
    print(f"  knife SFX: FAIL (unexpected IDs: {bad[:5]})")
    sys.exit(1)

print(f"  knife SFX: PASS ({len(seen)} whoosh submit(s); first frame {seen[0][0]}, sound {seen[0][1]})")
PY

echo "knife throw SFX smoke: PASS"
echo "artifacts: $OUT_DIR"
