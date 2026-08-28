#!/bin/bash
#
# grenade_round_spawn_mtx_smoke.sh -- FID-0085 regression: the grenade-round
# (grenade launcher / flare pistol / piton gun) projectile init matrix.
#
# sub_GAME_7F05F73C (gun.c) copies the projectile's initial rotation matrix from
# &hands[hand].throw_item_pos_related. Retail US ASM reads that field as the raw
# N64 byte offset g_CurrentPlayer + hand*936 + 0xAD8 (hands@0x870, field@+0x268).
# The native port body left hand_offset pinned to 0 AND kept the raw offset, so
# on the 64-bit layout -- where pointer fields inside struct hand expand the real
# field off 0x268 (throw_item_pos_related sits at native 0x274; struct hand is
# 968B not 936B; see tools/... offset probe / tests/test_struct_layout.c) --
# player+0xAD8 reads an unrelated hand interior (the blendpos region) as a
# rotation matrix, and the pinned offset drops the per-hand stride. The fix reads
# the real per-hand C field (default-ON); GE007_NO_PROJECTILE_INIT_MTX_FIX
# restores the raw read byte-identically.
#
# The sibling FID-0087 covers the same function's SPAWN POSITION: retail passes
# hand+0x2E8 = &hand->field_B58 (native 0x2F4, +0xC), and the legacy raw read
# (GE007_NO_GRENADE_SPAWN_POS_FIX) lands on field_B4C/B50/B54. This smoke guards
# both: the fix-OFF run restores BOTH raw reads.
#
# This smoke boots Dam, gives+equips the grenade launcher, and fires it twice --
# once fix-ON (default) and once fix-OFF -- capturing the copied matrix AND the
# spawn position via GE007_TRACE_PROJECTILE_INIT_MTX. It asserts:
#   (1) both runs actually fired a round (>=1 matrix trace each);
#   (2) fix-ON matrix differs from fix-OFF (FID-0085 load-bearing -- fails on revert);
#   (3) fix-ON is a real rotation basis (each row magnitude ~1.0), while the
#       legacy raw read is not (magnitude check separates them);
#   (4) fix-ON spawn position differs from fix-OFF (FID-0087 load-bearing);
#   (5) neither process crashed.
#
# ROM-derived captures stay local; do not commit them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_grenade_round_mtx_$$"
LEVEL=33
ITEM=24            # ITEM_GRENADELAUNCH
GIVE_FRAME=70
EQUIP_FRAME=85
FIRE_SPEC="110:60"
FRAMES=200

usage() {
    cat <<'USAGE'
Usage: tools/grenade_round_spawn_mtx_smoke.sh [options]

Options:
  --level N            raw LEVELID (default: 33 = Dam)
  --item N             weapon item id (default: 24 = GRENADELAUNCH; 35 flare, 36 piton)
  --give-frame N       frame to add weapon + ammo (default: 70)
  --equip-frame N      frame to equip the weapon (default: 85)
  --fire-spec F:L      AUTO_FIRE window frame:len (default: 110:60)
  --frames N           capture/exit frame (default: 200)
  --out-dir DIR        output dir (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary (default: build/ge007)
  --build-dir DIR      CMake build dir (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    process timeout (default: 120)

Artifacts are ROM-derived local validation data; do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="$2"; shift 2 ;;
        --item) ITEM="$2"; shift 2 ;;
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

# Fire the grenade launcher once under one flag state; capture the init-matrix trace.
run_state() {
    local label="$1"; shift          # "fixon" | "fixoff"
    local extra_env=("$@")           # extra KEY=VAL pairs (the negative control)
    local log="$OUT_DIR/run_${label}.log"
    local rc=0

    ( cd "$OUT_DIR" && validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_TRACE_PROJECTILE_INIT_MTX=1 \
            GE007_AUTO_ADD_ITEM="$ITEM" GE007_AUTO_ADD_ITEM_FRAME="$GIVE_FRAME" \
            GE007_AUTO_ADD_WEAPON_AMMO="$ITEM" GE007_AUTO_ADD_WEAPON_AMMO_AMOUNT=50 \
            GE007_AUTO_ADD_WEAPON_AMMO_FRAME="$GIVE_FRAME" \
            GE007_AUTO_EQUIP_ITEM="$ITEM" GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_FRAME" \
            GE007_AUTO_FIRE="$FIRE_SPEC" \
            ${extra_env[@]+"${extra_env[@]}"} \
            "$BINARY" --rom "$ROM" --level "$LEVEL" --deterministic \
            --screenshot-frame "$FRAMES" --screenshot-label "grmtx_${label}_$$" \
            --screenshot-exit ) >"$log" 2>&1 || rc=$?
    echo "$rc"
}

echo "== grenade-round init matrix A/B: level $LEVEL, item $ITEM, fire $FIRE_SPEC =="

RC_ON="$(run_state fixon)"
# fix-OFF restores BOTH sibling raw reads in sub_GAME_7F05F73C: the FID-0085
# projectile init matrix AND the FID-0087 spawn position -- so the run is the
# negative control for both.
RC_OFF="$(run_state fixoff GE007_NO_PROJECTILE_INIT_MTX_FIX=1 GE007_NO_GRENADE_SPAWN_POS_FIX=1)"

LOG_ON="$OUT_DIR/run_fixon.log"
LOG_OFF="$OUT_DIR/run_fixoff.log"

fail() { echo "grenade-round init matrix smoke: FAIL -- $1"; echo "artifacts: $OUT_DIR"; exit 1; }

# (4) survival
[[ "$RC_ON" -eq 0 ]]  || fail "fix-ON process exited $RC_ON"
[[ "$RC_OFF" -eq 0 ]] || fail "fix-OFF process exited $RC_OFF"

# (1) both fired
FIRST_ON="$(grep -F "[PROJ_INIT_MTX]" "$LOG_ON"  | head -1 || true)"
FIRST_OFF="$(grep -F "[PROJ_INIT_MTX]" "$LOG_OFF" | head -1 || true)"
[[ -n "$FIRST_ON"  ]] || fail "fix-ON fired no grenade round (no init-matrix trace)"
[[ -n "$FIRST_OFF" ]] || fail "fix-OFF fired no grenade round (no init-matrix trace)"

# Extract the r0/r1/r2 basis tuples (drop the translation, which is zeroed downstream).
basis() { sed -E 's/.* (r0=\([^)]*\) r1=\([^)]*\) r2=\([^)]*\)) .*/\1/'; }
BASIS_ON="$(printf '%s\n' "$FIRST_ON"  | basis)"
BASIS_OFF="$(printf '%s\n' "$FIRST_OFF" | basis)"

echo "  fix-ON  basis: $BASIS_ON"
echo "  fix-OFF basis: $BASIS_OFF"

# (2) load-bearing: the two paths must read different memory -> different basis.
[[ "$BASIS_ON" != "$BASIS_OFF" ]] || \
    fail "fix-ON and fix-OFF basis identical -- the FID-0085 fix is not load-bearing (reverted?)"

# (3) fix-ON is a real rotation: every row magnitude ~1.0 (tol 0.02).
check_orthonormal() {
    printf '%s\n' "$1" | grep -oE 'r[0-9]=\([^)]*\)' | \
    python3 -c '
import sys, re
ok = True
for line in sys.stdin:
    nums = [float(x) for x in re.findall(r"-?[0-9.]+", line.split("=",1)[1])]
    mag = sum(v*v for v in nums) ** 0.5
    if abs(mag - 1.0) > 0.02:
        ok = False
        print(f"    non-unit row {line.strip()} |mag|={mag:.4f}")
sys.exit(0 if ok else 1)
'
}
if check_orthonormal "$BASIS_ON"; then
    echo "  fix-ON basis is orthonormal (real rotation): PASS"
else
    fail "fix-ON basis rows are not unit-length -- not a valid throw_item_pos_related rotation"
fi

# --- FID-0087: grenade-round SPAWN POSITION fail-on-revert ---
# fix-ON reads &hand->field_B58; fix-OFF the raw hand+0x2E8 interior. The two
# must resolve to different memory -> different spawn position.
POS_ON="$(grep -F "[PROJ_SPAWN_POS]" "$LOG_ON"  | head -1 | sed -E 's/.*(pos=\([^)]*\)).*/\1/' || true)"
POS_OFF="$(grep -F "[PROJ_SPAWN_POS]" "$LOG_OFF" | head -1 | sed -E 's/.*(pos=\([^)]*\)).*/\1/' || true)"
[[ -n "$POS_ON"  ]] || fail "fix-ON emitted no spawn-position trace (FID-0087 probe missing)"
[[ -n "$POS_OFF" ]] || fail "fix-OFF emitted no spawn-position trace (FID-0087 probe missing)"
echo "  fix-ON  spawn pos: $POS_ON"
echo "  fix-OFF spawn pos: $POS_OFF"
[[ "$POS_ON" != "$POS_OFF" ]] || \
    fail "fix-ON and fix-OFF spawn position identical -- the FID-0087 fix is not load-bearing (reverted?)"
echo "  FID-0087 spawn position is load-bearing (fix-ON != fix-OFF): PASS"

echo "grenade-round init matrix + spawn position smoke: PASS"
echo "artifacts: $OUT_DIR"
exit 0
