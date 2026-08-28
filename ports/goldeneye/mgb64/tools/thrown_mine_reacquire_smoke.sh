#!/bin/bash
#
# thrown_mine_reacquire_smoke.sh -- FID-0079 end-to-end regression: the live
# flag-8 projectile tile-reacquire path (chrobjhandler.c:5328 -> stan.c
# sub_GAME_7F0AF20C roomset filter).
#
# FID-0079: retail's roomset filter (sub_GAME_7F0AF20C, ASM stan.c:1063-1086)
# reads u8 room bytes (0xFF-terminated); the prior port cast the u8 roomset[8] to
# s32* and never matched any room, so a thrown mine whose stan tile is invalidated
# mid-flight could never REACQUIRE a floor tile -- flag 8 stayed set forever
# (floor logic degraded). The platform-TU unit lane (ctest stan_roomset) pins the
# pure filter; THIS smoke is the missing end-to-end guard (P1d-residual class):
# it drives the whole live path in-game.
#
# Repro (deterministic, mission 1 = race-free per FID-0075): boot Dam, give +
# equip a PROXIMITY MINE (item 28), walk forward and LOB it (look up) so it arcs
# and descends onto a lower floor. As it drops, walkTilesBetweenPoints_NoCallback
# from the mine's stale tile fails -> handles_projectile_motion sets projectile
# flag 8 (projflags 0x47 -> 0x4f). The next tick calls sub_GAME_7F0AF20C(pos,
# roomset) to REACQUIRE: with the byte fix (default ON) the room is admitted and
# the floor tile is found -> flag 8 CLEARS (0x4f -> 0x47) and prop->pos advances;
# under GE007_NO_STAN_ROOMSET_BYTE_FIX=1 the s32 misread rejects every room ->
# reacquire returns NULL -> flag 8 stays stuck -> a different whole-sim outcome.
#
# Asserts (all reddening if the fix is reverted):
#   (1) both runs survive;
#   (2) both runs ENTER the flag-8 path (projflags 0x4f appears in each) -- the
#       tile-invalidation is polarity-independent, so the ONLY difference is the
#       reacquire result;
#   (3) fix-ON REACQUIRES (a 0x4f -> 0x47 flag-8-clear transition) while fix-OFF
#       does NOT (flag 8 never clears) -- the FID-0079 path is load-bearing;
#   (4) the fix-ON whole-sim state hash DIFFERS from fix-OFF (fail-on-revert at
#       the determinism level).
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
OUT_DIR="/tmp/mgb64_thrown_mine_reacquire_$$"
LEVEL=33            # Dam (mission 1, race-free per FID-0075)
ITEM=28            # ITEM_PROXIMITYMINE
GIVE_FRAME=80
AMMO_FRAME=90
EQUIP_FRAME=130
FORWARD_SPEC="140:40"
LOOKUP_SPEC="150:25"
FIRE_SPEC="185:8"
END_TIMER=360

usage() {
    cat <<'USAGE'
Usage: tools/thrown_mine_reacquire_smoke.sh [options]

Options:
  --level N            raw LEVELID (default: 33 = Dam)
  --item N             mine item id (default: 28 = PROXIMITYMINE)
  --end-timer N        game-timer at which the sim-state hash is captured (default: 360)
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
        --end-timer) END_TIMER="$2"; shift 2 ;;
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

# ROM/binary-gated: SKIP cleanly (exit 0) when prerequisites are absent, like the
# other tier-3 lanes -- keeps ROM-free ctest runs green.
if [[ ! -x "$BINARY" || ! -e "$ROM" ]]; then
    echo "thrown-mine reacquire smoke: SKIP (native binary or ROM absent)"
    exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

# Throw one proximity mine under one flag state; capture the projectile trace +
# the whole-sim state hash. Prints the captured 16-hex sim hash on stdout.
run_state() {
    local label="$1"; shift          # "fixon" | "fixoff"
    local extra_env=("$@")           # extra KEY=VAL pairs (the negative control)
    local log="$OUT_DIR/run_${label}.log"
    local hj="$OUT_DIR/run_${label}.hash.json"
    local rc=0

    ( cd "$OUT_DIR" && validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_PROJECTILE_TRACE=1 GE007_PROJECTILE_TRACE_BUDGET=20000 \
            GE007_AUTO_ADD_ITEM="$ITEM" GE007_AUTO_ADD_ITEM_FRAME="$GIVE_FRAME" \
            GE007_AUTO_ADD_WEAPON_AMMO="$ITEM" GE007_AUTO_ADD_WEAPON_AMMO_AMOUNT=10 \
            GE007_AUTO_ADD_WEAPON_AMMO_FRAME="$AMMO_FRAME" \
            GE007_AUTO_EQUIP_ITEM="$ITEM" GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_FRAME" \
            GE007_AUTO_FORWARD="$FORWARD_SPEC" GE007_AUTO_LOOK_UP="$LOOKUP_SPEC" \
            GE007_AUTO_FIRE="$FIRE_SPEC" \
            ${extra_env[@]+"${extra_env[@]}"} \
            "$BINARY" --rom "$ROM" --level "$LEVEL" --deterministic \
            --screenshot-game-timer "$END_TIMER" --screenshot-exit \
            --sim-state-hash-out "$hj" ) >"$log" 2>&1 || rc=$?
    echo "$rc" > "$OUT_DIR/run_${label}.rc"
    grep -o '[0-9a-f]\{16\}' "$hj" 2>/dev/null | head -1
}

echo "== thrown-mine tile reacquire A/B: level $LEVEL, item $ITEM, lob $FORWARD_SPEC/$LOOKUP_SPEC/$FIRE_SPEC =="

HASH_ON="$(run_state fixon)"
HASH_OFF="$(run_state fixoff GE007_NO_STAN_ROOMSET_BYTE_FIX=1)"

LOG_ON="$OUT_DIR/run_fixon.log"
LOG_OFF="$OUT_DIR/run_fixoff.log"
RC_ON="$(cat "$OUT_DIR/run_fixon.rc")"
RC_OFF="$(cat "$OUT_DIR/run_fixoff.rc")"

fail() { echo "thrown-mine reacquire smoke: FAIL -- $1"; echo "artifacts: $OUT_DIR"; exit 1; }

# (1) survival
[[ "$RC_ON"  -eq 0 ]] || fail "fix-ON process exited $RC_ON"
[[ "$RC_OFF" -eq 0 ]] || fail "fix-OFF process exited $RC_OFF"

# Emit the flag-8 bit (0/1) for each projectile-motion trace record, in order.
flag8_seq() {
    grep -o 'projflags=0x[0-9a-f]*' "$1" | while read -r p; do
        v=$(( 16#${p#projflags=0x} ))
        echo $(( (v & 8) ? 1 : 0 ))
    done | tr -d '\n'
}
SEQ_ON="$(flag8_seq "$LOG_ON")"
SEQ_OFF="$(flag8_seq "$LOG_OFF")"

# (2) both runs ENTER the flag-8 path (tile invalidated) -> the roomset caller runs.
[[ "$SEQ_ON"  == *1* ]] || fail "fix-ON never entered the flag-8 tile-reacquire path (no thrown-mine walk failure captured)"
[[ "$SEQ_OFF" == *1* ]] || fail "fix-OFF never entered the flag-8 tile-reacquire path (scenario did not fire)"

# (3) load-bearing: fix-ON REACQUIRES (flag 8 set then CLEARED = "10" in the seq)
#     while fix-OFF stays stuck (no clear-after-set).
[[ "$SEQ_ON"  == *10* ]] || fail "fix-ON did not reacquire the tile (no flag-8 clear 0x4f->0x47) -- the byte fix is not exercised (reverted?)"
if [[ "$SEQ_OFF" == *10* ]]; then
    fail "fix-OFF ALSO cleared flag 8 -- the s32 misread reacquired a tile it must reject; the FID-0079 fix is not load-bearing (reverted?)"
fi
echo "  fix-ON reacquired the mine's tile (flag-8 clear present); fix-OFF stayed stuck: PASS"

# (4) fail-on-revert at the whole-sim level.
[[ -n "$HASH_ON"  ]] || fail "fix-ON produced no sim-state hash"
[[ -n "$HASH_OFF" ]] || fail "fix-OFF produced no sim-state hash"
echo "  fix-ON  sim hash: $HASH_ON"
echo "  fix-OFF sim hash: $HASH_OFF"
[[ "$HASH_ON" != "$HASH_OFF" ]] || \
    fail "fix-ON and fix-OFF whole-sim hash identical -- the FID-0079 reacquire is not load-bearing (reverted?)"
echo "  whole-sim outcome differs fix-ON vs fix-OFF (fail-on-revert): PASS"

echo "thrown-mine reacquire smoke: PASS"
echo "artifacts: $OUT_DIR"
exit 0
