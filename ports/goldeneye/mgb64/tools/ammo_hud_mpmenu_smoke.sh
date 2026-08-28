#!/bin/bash
#
# ammo_hud_mpmenu_smoke.sh -- FID-0064 regression lane.
#
# Retail's generate_ammo_total_microcode (gun.c glabel, US 7F069CF4) has TWO
# independent !=0 early-outs in its prologue: gunammooff (0x1064) AND mpmenuon
# (0x29c4, gun.c:32354-32356). The port had dropped the second, so in
# multiplayer with the watch/pause menu open a pane still drew its ammo HUD over
# the darkening overlay. This lane proves the restored early-out suppresses the
# per-pane ammo HUD when that pane's mpmenuon is set, and reproduces the old
# draw under the GE007_NO_MP_AMMO_HUD_MENU_FIX negative control.
#
# Recipe: boot a 2-player split-screen deathmatch (temple), equip the P2 pane
# with the PP7 (item 4) + ammo, force the MP watch menu open on every pane
# (GE007_AUTO_MPMENU -- deterministic stand-in for a per-pane Start press, which
# the scripted-input router can only deliver to pad 0), and screenshot. Three
# deterministic runs that differ in exactly one variable:
#   closed   equip + ammo, NO menu               -> P2 ammo HUD drawn (control)
#   open_on  equip + ammo, menu open, fix ON     -> P2 ammo HUD HIDDEN
#   open_off equip + ammo, menu open, fix OFF     -> P2 ammo HUD drawn (legacy)
# open_on vs open_off differ ONLY by the fix flag, so any pixel delta between
# them IS the ammo-HUD suppression; reverting the fix makes open_on draw the HUD
# too (delta -> 0) and reddens this lane.
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
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_ammo_hud_mpmenu_smoke_$$"
MP_STAGE="temple"
SCENARIO="deathmatch"
PLAYERS=2
EQUIP_FRAME=100
EQUIP_ITEM=4          # ITEM_WPPK / PP7
AMMO_FRAME=120
AMMO_MAG=7
AMMO_RESERVE=42
MENU_FRAME=150
SHOT_FRAME=250
SCREEN_WIDTH=640
SCREEN_HEIGHT=480
# open_on-vs-open_off delta is the ammo HUD alone (identical inputs otherwise):
# a PP7 icon + mag/reserve digits measured ~478px. Band rejects both a reverted
# fix (delta -> ~0) and any flag that suddenly perturbs far more than the HUD.
MIN_HUD_DIFF=150
MAX_HUD_DIFF=6000
# closed-vs-open_off must show the menu actually opened (darkening overlay).
MIN_MENU_DARKEN_PCT="15.0"

usage() {
    cat <<'USAGE'
Usage: tools/ammo_hud_mpmenu_smoke.sh [options]

Options:
  --players N            split-screen player count (default: 2)
  --mp-stage NAME|ID     multiplayer stage (default: temple)
  --scenario NAME|ID     combat scenario (default: deathmatch)
  --frames N             screenshot/exit frame (default: 250)
  --menu-frame N         frame to force the MP watch menu open (default: 150)
  --min-hud-diff N       minimum ammo-HUD suppression delta, px (default: 150)
  --max-hud-diff N       maximum ammo-HUD suppression delta, px (default: 6000)
  --out-dir DIR          output directory (default: /tmp/...)
  --rom PATH             ROM path (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --timeout SECONDS      per-run timeout (default: 120)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --players) PLAYERS="$2"; shift 2 ;;
        --mp-stage) MP_STAGE="$2"; shift 2 ;;
        --scenario) SCENARIO="$2"; shift 2 ;;
        --frames) SHOT_FRAME="$2"; shift 2 ;;
        --menu-frame) MENU_FRAME="$2"; shift 2 ;;
        --min-hud-diff) MIN_HUD_DIFF="$2"; shift 2 ;;
        --max-hud-diff) MAX_HUD_DIFF="$2"; shift 2 ;;
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

for value_name in PLAYERS SHOT_FRAME MENU_FRAME TIMEOUT_SECONDS MIN_HUD_DIFF MAX_HUD_DIFF; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: ${value_name} must be a positive integer: $value" >&2
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

FAILED=0

# run_capture LABEL EXTRA_ENV...
run_capture() {
    local label="$1"
    shift
    local shot_label="ammo_mpmenu_${label}_$$"

    rm -f "$OUT_DIR/${label}.log" "$OUT_DIR/${label}.bmp"
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
                GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_FRAME" \
                GE007_AUTO_EQUIP_ITEM="$EQUIP_ITEM" \
                GE007_AUTO_SET_HAND_AMMO_FRAME="$AMMO_FRAME" \
                GE007_AUTO_SET_HAND_AMMO_HAND=0 \
                GE007_AUTO_SET_HAND_AMMO_MAG="$AMMO_MAG" \
                GE007_AUTO_SET_HAND_AMMO_RESERVE="$AMMO_RESERVE" \
                "$@" \
                "$BINARY" \
                --rom "$ROM" \
                --savedir "$OUT_DIR" \
                --multiplayer \
                --players "$PLAYERS" \
                --mp-stage "$MP_STAGE" \
                --scenario "$SCENARIO" \
                --deterministic \
                --screenshot-frame "$SHOT_FRAME" \
                --screenshot-label "$shot_label" \
                --screenshot-exit
    ) >"$OUT_DIR/${label}.log" 2>&1; then
        echo "  run ${label}: FAIL (process)"
        tail -12 "$OUT_DIR/${label}.log" | sed 's/^/    /'
        return 1
    fi
    if [[ ! -f "$OUT_DIR/screenshot_${shot_label}.bmp" ]]; then
        echo "  run ${label}: FAIL (no screenshot)"
        return 1
    fi
    mv "$OUT_DIR/screenshot_${shot_label}.bmp" "$OUT_DIR/${label}.bmp"
    echo "  run ${label}: ok"
    return 0
}

echo "=== FID-0064 MP-menu ammo-HUD suppression smoke ==="
echo "  out-dir:  $OUT_DIR"
echo "  binary:   $BINARY"
echo "  stage:    $MP_STAGE / $SCENARIO / ${PLAYERS}p"

run_capture closed                                                     || FAILED=1
run_capture open_on   GE007_AUTO_MPMENU="$MENU_FRAME"                   || FAILED=1
run_capture open_off  GE007_AUTO_MPMENU="$MENU_FRAME" \
                      GE007_NO_MP_AMMO_HUD_MENU_FIX=1                   || FAILED=1

if [[ "$FAILED" -ne 0 ]]; then
    echo "FAIL: capture stage failed (artifacts in $OUT_DIR)"
    exit 1
fi

# Assertion 1: the menu actually opened (darkening overlay present) -- guards
# against GE007_AUTO_MPMENU silently no-op'ing and the whole test passing on a
# trivially-empty delta.
echo "  [assert] menu opened (darkening overlay)"
if ! python3 - "$OUT_DIR/closed.bmp" "$OUT_DIR/open_off.bmp" "$MIN_MENU_DARKEN_PCT" <<'PY'
import sys
import numpy as np
from PIL import Image

a = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.int16)
b = np.asarray(Image.open(sys.argv[2]).convert("RGB"), dtype=np.int16)
if a.shape != b.shape:
    print(f"    FAIL: size mismatch {a.shape} vs {b.shape}")
    raise SystemExit(1)
changed_mask = np.any(a != b, axis=2)
pct = 100.0 * int(changed_mask.sum()) / changed_mask.size
min_pct = float(sys.argv[3])
print(f"    menu-open darkening changed={pct:.2f}% (min {min_pct:.2f}%)")
raise SystemExit(0 if pct >= min_pct else 1)
PY
then
    echo "  [assert] menu opened: FAIL (no darkening -- GE007_AUTO_MPMENU inert?)"
    exit 1
fi
echo "  [assert] menu opened: PASS"

# Assertion 2 (fail-on-revert): open_on vs open_off differ ONLY by the fix flag,
# so their delta IS the suppressed ammo HUD. Require a localized delta in the
# expected band, confined to a single split-screen pane.
echo "  [assert] fix suppresses per-pane ammo HUD"
if ! python3 - "$OUT_DIR/open_on.bmp" "$OUT_DIR/open_off.bmp" \
        "$MIN_HUD_DIFF" "$MAX_HUD_DIFF" "$SCREEN_HEIGHT" <<'PY'
import sys
import numpy as np
from PIL import Image

on = np.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=np.int16)   # fix ON -> HUD hidden
off = np.asarray(Image.open(sys.argv[2]).convert("RGB"), dtype=np.int16)  # fix OFF -> HUD drawn
if on.shape != off.shape:
    print(f"    FAIL: size mismatch {on.shape} vs {off.shape}")
    raise SystemExit(1)
lo = int(sys.argv[3])
hi = int(sys.argv[4])
height = int(sys.argv[5])
split = height // 2

changed = np.any(on != off, axis=2)
total = int(changed.sum())
top = int(changed[:split, :].sum())
bottom = total - top
rows = np.any(changed, axis=1)
cols = np.any(changed, axis=0)
if total:
    y0, y1 = int(np.argmax(rows)), int(len(rows) - np.argmax(rows[::-1]))
    x0, x1 = int(np.argmax(cols)), int(len(cols) - np.argmax(cols[::-1]))
    bbox = (x0, y0, x1, y1)
else:
    bbox = None

print(f"    ammo-HUD delta total={total} (band {lo}..{hi})  "
      f"top-pane={top} bottom-pane={bottom}  bbox={bbox}")

if total < lo:
    print("    FAIL: HUD not suppressed -- open_on and open_off match "
          "(fix reverted, or the pane never held an ammo weapon)")
    raise SystemExit(1)
if total > hi:
    print("    FAIL: delta far exceeds an ammo HUD -- flag perturbs more than "
          "the early-out")
    raise SystemExit(1)
# The delta must be confined to ONE pane's HUD corner: the other pane is byte
# identical between the two runs.
if top != 0 and bottom != 0:
    print("    FAIL: delta spans both panes -- not a single per-pane ammo HUD")
    raise SystemExit(1)
raise SystemExit(0)
PY
then
    echo "  [assert] fix suppresses per-pane ammo HUD: FAIL"
    exit 1
fi
echo "  [assert] fix suppresses per-pane ammo HUD: PASS"

echo ""
echo "=== FID-0064 MP-menu ammo-HUD suppression smoke: PASS ==="
echo "  artifacts: $OUT_DIR"
exit 0
