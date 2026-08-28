#!/bin/bash
#
# shoot_out_lights_regression.sh -- Headless proof of "shoot out the lights"
# (GE007_SHOOT_OUT_LIGHTS, W4.E1). Drives a real aimed shot at a Bunker light
# fixture and asserts:
#   (a) the fixture visibly darkens  -- ROI mean-luma drops >= 30% (pre -> post),
#   (b) the darkening persists on re-entry -- after the player leaves the room's
#       view and returns, the fixture ROI is byte-identical to the post-shot ROI
#       (<= 1.0% changed),
#   (c) the feature is opt-in / gated -- with GE007_SHOOT_OUT_LIGHTS=0 the same
#       route does NOT darken the fixture (ROI stays lit), and the pre-fire frame
#       is byte-identical between flag-on and flag-off (cmp 0.000%).
#
# Route: Bunker (bunker1), warp to a pad in the control-room area, then force the
# player directly below the room-5 linear lamp aiming up (clear vertical LOS) and
# fire a sustained burst. Coordinates were measured headless (W4.E1.T3); the lamp
# is IMAGE_LINEAR_LAMP (check_if_imageID_is_light).
#
# NOTE on re-entry: on native, room aging/unloading is disabled (bg.c
# sub_GAME_7F0B66E8 returns early -- "all rooms stay loaded"), so a room is not
# freed mid-level. "Re-entry" therefore means the player physically leaves the
# fixture's view (teleports to a far room) and returns; the darkening persists via
# the retained Vtx.cn shade bytes (and redarken_lights_in_room re-applies it on any
# actual reload -- level load / late on-demand load). The fixture staying dark on
# return is the persistence guarantee a player sees.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# screenshots, logs, or summaries.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

# ---- route constants (bunker1 room-5 linear lamp; measured W4.E1.T3) ----
LEVEL="bunker1"
WARP_PAD=20
WARP_FRAME=40
POSE_LAMP="-74.2:180:1840.6:0:80.8"      # x:y:z:yaw:pitch -- below the lamp, aiming up
POSE_AWAY="0:200:-2000:0:0"               # a far room; lamp out of view
FIRE="75:40"                              # Z-trig burst: frame 75 for 40 frames
PRE_FRAME=72                              # after aim (60), before fire (75): lamp lit
POST_FRAME=130                            # after fire ends (115): lamp darkened
REENTRY_FRAME=365                         # after leave (170) + return (280): still dark
LAMP_ROI="170,100,64,64"                  # X,Y,W,H over the lit fixture (640x480)
MIN_LUMA_DROP_PCT=30.0                     # (a) fixture must dim by this much
MAX_REENTRY_CHANGED_PCT=1.0               # (b) re-entry vs post-shot ROI budget
MAX_OFF_DROP_PCT=5.0                       # (c) OFF run must NOT darken (drop below this)

FORCE_SINGLE="60-150:${POSE_LAMP}"
FORCE_REENTRY="60-150:${POSE_LAMP},170-260:${POSE_AWAY},280-370:${POSE_LAMP}"

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=180
OUT_DIR="/tmp/mgb64_shoot_out_lights_$$"

usage() {
    cat <<'USAGE'
Usage: tools/shoot_out_lights_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-capture timeout (default: 180)

Artifacts are ROM-derived local validation data. Do not commit captured
screenshots, logs, or generated summaries.
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
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

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

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

TIMEOUT_CMD="$(validation_resolve_timeout_cmd)"

echo "=== Shoot-Out-The-Lights Regression (W4.E1) ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL  warp-pad: $WARP_PAD  lamp-ROI: $LAMP_ROI"

# capture <label> <flag 0|1> <force-script> <fire> <frame>
capture() {
    local label="$1" flag="$2" force="$3" fire="$4" frame="$5"
    local shot="$OUT_DIR/screenshot_${label}.bmp"
    rm -f "$shot"
    (
        cd "$OUT_DIR"
        local -a run=(
            "$BINARY" --rom "$ROM" --level "$LEVEL" --deterministic
            --config-override Video.RenderScale=1
            --config-override Video.MSAA=0
            --config-override Video.Ssao=0
            --screenshot-frame "$frame" --screenshot-exit --screenshot-label "$label"
        )
        if [[ -n "$TIMEOUT_CMD" ]]; then
            validation_automation_env \
                GE007_SHOOT_OUT_LIGHTS="$flag" \
                GE007_AUTO_WARP_FRAME="$WARP_FRAME" GE007_AUTO_WARP_PAD="$WARP_PAD" \
                GE007_AUTO_FORCE_PLAYER_SCRIPT="$force" \
                GE007_AUTO_FIRE="$fire" \
                "$TIMEOUT_CMD" --kill-after=5 "$TIMEOUT_SECONDS" "${run[@]}"
        else
            validation_automation_env \
                GE007_SHOOT_OUT_LIGHTS="$flag" \
                GE007_AUTO_WARP_FRAME="$WARP_FRAME" GE007_AUTO_WARP_PAD="$WARP_PAD" \
                GE007_AUTO_FORCE_PLAYER_SCRIPT="$force" \
                GE007_AUTO_FIRE="$fire" \
                "${run[@]}"
        fi
    ) >"$OUT_DIR/${label}.log" 2>&1
    if [[ ! -f "$shot" ]]; then
        echo "FAIL: capture '$label' produced no screenshot (see $OUT_DIR/${label}.log)" >&2
        exit 1
    fi
    printf '%s\n' "$shot"
}

echo ""
echo "-- capturing (ON: pre-shot, post-shot, re-entry; OFF: pre-shot, post-shot) --"
ON_PRE="$(capture on_pre     1 "$FORCE_SINGLE"  "$FIRE" "$PRE_FRAME")"
ON_POST="$(capture on_post    1 "$FORCE_SINGLE"  "$FIRE" "$POST_FRAME")"
ON_REENTRY="$(capture on_reentry 1 "$FORCE_REENTRY" "$FIRE" "$REENTRY_FRAME")"
OFF_PRE="$(capture off_pre    0 "$FORCE_SINGLE"  "$FIRE" "$PRE_FRAME")"
OFF_POST="$(capture off_post   0 "$FORCE_SINGLE"  "$FIRE" "$POST_FRAME")"

# ---- (a) fixture darkens: ON ROI mean-luma drop >= 30% ----
echo ""
echo "-- (a) fixture darkening (ON pre -> post, ROI $LAMP_ROI) --"
python3 - "$ON_PRE" "$ON_POST" "$OFF_POST" "$LAMP_ROI" "$MIN_LUMA_DROP_PCT" "$MAX_OFF_DROP_PCT" <<'PY'
import sys
from PIL import Image
on_pre, on_post, off_post, roi_s, min_drop, max_off_drop = sys.argv[1:7]
x, y, w, h = (int(v) for v in roi_s.split(","))
min_drop = float(min_drop); max_off_drop = float(max_off_drop)

def luma(path):
    px = Image.open(path).convert("RGB").crop((x, y, x + w, y + h)).get_flattened_data()
    return sum(0.299 * r + 0.587 * g + 0.114 * b for r, g, b in px) / len(px)

pre = luma(on_pre); post = luma(on_post); offp = luma(off_post)
on_drop = 100.0 * (pre - post) / pre if pre > 0 else 0.0
off_drop = 100.0 * (pre - offp) / pre if pre > 0 else 0.0
print(f"  ON  ROI luma: pre={pre:.1f} post={post:.1f} drop={on_drop:.1f}% (>= {min_drop}%)")
print(f"  OFF ROI luma: pre={pre:.1f} post={offp:.1f} drop={off_drop:.1f}% (< {max_off_drop}%)")

fail = False
if on_drop < min_drop:
    print(f"FAIL: (a) fixture did not darken: ON drop {on_drop:.1f}% < {min_drop}%", file=sys.stderr); fail = True
if off_drop >= max_off_drop:
    print(f"FAIL: (c1) OFF run darkened the fixture: drop {off_drop:.1f}% >= {max_off_drop}%", file=sys.stderr); fail = True
sys.exit(1 if fail else 0)
PY

# ---- (b) persistence: re-entry ROI ~ post-shot ROI (<= 1.0% changed) ----
echo ""
echo "-- (b) persistence on re-entry (post vs re-entry, ROI $LAMP_ROI) --"
python3 tools/compare_screenshots.py "$ON_POST" "$ON_REENTRY" \
    --roi "$LAMP_ROI" --max-changed-pct "$MAX_REENTRY_CHANGED_PCT"

# ---- (c2) OFF gated: pre-fire frame byte-identical between flag on/off ----
echo ""
echo "-- (c2) OFF gating: pre-fire frame byte-identical (ON vs OFF) --"
if cmp -s "$ON_PRE" "$OFF_PRE"; then
    echo "  cmp on_pre off_pre: identical (0.000% changed)"
else
    echo "FAIL: (c2) pre-fire frame differs between flag-on and flag-off" >&2
    python3 tools/compare_screenshots.py "$ON_PRE" "$OFF_PRE" 2>&1 | grep -i 'changed pixels' >&2 || true
    exit 1
fi

echo ""
echo "PASS: shoot-out-the-lights darkens the fixture (>= ${MIN_LUMA_DROP_PCT}% ROI luma drop),"
echo "      persists on re-entry (<= ${MAX_REENTRY_CHANGED_PCT}% ROI change), and is gated when off."
echo "artifacts: $OUT_DIR (local only -- do not commit)"
