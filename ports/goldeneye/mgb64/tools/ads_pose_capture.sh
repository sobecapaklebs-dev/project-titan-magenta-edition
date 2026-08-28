#!/bin/bash
#
# ads_pose_capture.sh -- author/verify the ADS "rise to sights" weapon pose.
#
# Boots a solo mission, force-equips a weapon, holds aim (R_TRIG) across the
# capture frame with ADS enabled, applies an optional GE007_ADS_POSE_* override,
# captures a screenshot and converts it to PNG for inspection. Captures/BMPs are
# ROM-derived local validation data and must NOT be committed (see .gitignore).
#
# Usage:
#   tools/ads_pose_capture.sh LABEL ITEM [X Y Z YAWDEG PITCHDEG ROLLDEG]
# Env overrides: MISSION, CAP (capture frame), ADS (0/1), BIN, ROM, OUTDIR.
#
# Item ids: 4=PP7(WPPK) 5=PP7sil 8=KF7(AK47) 13=AR33(M16) 14=RC-P90(FNP90)
#           17=Sniper 15=Shotgun 9=ZMG(UZI) 10=D5K(MP5K)
#
set -euo pipefail
cd "$(dirname "$0")/.."

LABEL="${1:?need a label}"
ITEM="${2:?need an item id}"
X="${3:-}"; Y="${4:-}"; Z="${5:-}"; YAW="${6:-}"; PITCH="${7:-}"; ROLL="${8:-}"

MISSION="${MISSION:-1}"
CAP="${CAP:-700}"
ADS="${ADS:-1}"
BIN="${BIN:-./build/ge007}"
OUTDIR="${OUTDIR:-/tmp/ads_pose}"
mkdir -p "$OUTDIR"

EQUIP_ADD=120; EQUIP_DO=160
AIM_START=$(( CAP > 260 ? CAP - 260 : 40 ))
# Briefly pitch the view down before aiming so the viewmodel is framed
# consistently at the bottom of the shot (then hold — no further look input).
LOOK_DOWN_START=$(( AIM_START - 60 )); LOOK_DOWN_LEN=24; LOOK_STEP="${LOOK_STEP:-9}"

# Optional pose overrides — only export the axes that were passed.
poseenv=()
[ -n "$X" ]     && poseenv+=("GE007_ADS_POSE_X=$X")
[ -n "$Y" ]     && poseenv+=("GE007_ADS_POSE_Y=$Y")
[ -n "$Z" ]     && poseenv+=("GE007_ADS_POSE_Z=$Z")
[ -n "$YAW" ]   && poseenv+=("GE007_ADS_POSE_YAW_DEG=$YAW")
[ -n "$PITCH" ] && poseenv+=("GE007_ADS_POSE_PITCH_DEG=$PITCH")
[ -n "$ROLL" ]  && poseenv+=("GE007_ADS_POSE_ROLL_DEG=$ROLL")

timeout 130 env \
  GE007_AUTO_ADD_ITEM_FRAME="$EQUIP_ADD" GE007_AUTO_ADD_ITEM="$ITEM" \
  GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_DO" GE007_AUTO_EQUIP_ITEM="$ITEM" \
  GE007_AUTO_LOOK_DOWN="${LOOK_DOWN_START}:${LOOK_DOWN_LEN}" GE007_AUTO_LOOK_STEP="$LOOK_STEP" \
  GE007_AUTO_AIM="${AIM_START}:400" \
  ${poseenv[@]+"${poseenv[@]}"} \
  "$BIN" --mission "$MISSION" --difficulty 0 \
    --savedir "$OUTDIR" \
    --config-override "Input.AdsEnabled=$ADS" \
    --screenshot-frame "$CAP" --screenshot-exit \
    --screenshot-label "$LABEL" --background --no-input-grab >/dev/null 2>&1 || true

# NOTE: --savedir relocates ge007.ini/eeprom into $OUTDIR so this capture never
# pollutes the user's repo-root ge007.ini (e.g. persisting Input.AdsEnabled or a
# stray --config-override). The screenshot is written cwd-relative regardless, so
# it lands in the repo root; convert it to PNG under $OUTDIR, then remove the BMP.
if [ -f "screenshot_${LABEL}.bmp" ]; then
    sips -s format png "screenshot_${LABEL}.bmp" --out "${OUTDIR}/${LABEL}.png" >/dev/null 2>&1
    rm -f "screenshot_${LABEL}.bmp"
    echo "OK ${OUTDIR}/${LABEL}.png  (item=$ITEM pose X=$X Y=$Y Z=$Z yaw=$YAW pitch=$PITCH roll=$ROLL)"
else
    echo "FAIL no screenshot for $LABEL"
fi
