#!/bin/bash
#
# streets_determinism_regression.sh -- FID-0046 regression: a vanilla stage must
# produce the SAME whole-sim state hash across two SEPARATE processes of the same
# seed + input under the determinism envelope.
#
# FID-0046 root cause: alloc_explosion_smoke_casing_scorch_impact_buffers()
# (src/game/initexplosioncasing.c) bump-allocates the explosion/smoke/scorch/
# impact/particle buffers from the stage pool immediately after the transient
# level-load file loads have used that same pool region as scratch, and only
# initializes each entry's "free" flag (Explosion.parts[j].frame, Smoke.size,
# Scorch.roomid, BulletImpact.room, FlyingParticles.unk00) + prop=NULL. The rest
# of every free entry (part pos/size/rot/bb, vertex lists, models, ...) keeps the
# leftover file-load bytes. The game never READS those fields while the entry is
# free, so on retail this is a benign uninitialized read made deterministic by
# fixed RDRAM addressing; under the port's ASLR, one of those leftover words is
# the low 32 bits of a converted host pointer (Streets/LEVELID 29 exposes it at
# g_ExplosionBuffer[12].parts[14].rot), which varies per process and lands in the
# whole-pool sim-state invariance hash -- so the stage is non-deterministic across
# processes even though gameplay (the screenshot) is byte-identical.
#
# Fix (initexplosioncasing.c, default ON): fully zero each effect buffer at
# allocation; GE007_NO_EFFECT_BUF_ZERO_INIT=1 restores the legacy leave-
# uninitialized behavior.
#
# This gate proves, all reddening if the fix is reverted:
#   (A) every run boots and emits a hash + screenshot;
#   (B) the rendered gameplay (screenshot) is byte-identical across all four runs
#       -- deterministic AND unaffected by the flag (byte-identity under opt-out);
#   (C) two fix-ON processes produce the IDENTICAL sim-state hash (the fix makes
#       the stage cross-process deterministic) -- THE core assertion;
#   (D) two fix-OFF processes produce DIFFERENT sim-state hashes (the pre-fix
#       defect: without the zero-init the stage is non-deterministic) -- so
#       removing the memset reddens assertion (C).
#
# ROM/binary-gated: SKIPs cleanly (exit 0) when the ROM or native binary is
# absent, so the ROM-free ctest suite (and CI) stays green.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=200
OUT_DIR="/tmp/mgb64_streets_determinism_$$"
LEVEL=29            # Streets (LEVELID 29) -- the FID-0046 repro level
END_TIMER=900      # 15 sim-seconds; matches the uncap_purity_gate default
DIAGNOSTIC_DUMPS=0

usage() {
    cat <<'USAGE'
Usage: tools/streets_determinism_regression.sh [options]

Options:
  --level N            raw LEVELID (default: 29 = Streets)
  --end-timer N        game-timer (g_GlobalTimer) at which the sim hash is
                       captured (default: 900)
  --out-dir DIR        output dir (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary (default: build/ge007)
  --build-dir DIR      CMake build dir (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-process timeout (default: 200)
  --diagnostic-dumps   emit per-region hashes plus raw and canonical pool dumps
                       for exact byte attribution (64 MiB total across 4 runs)

Runs the level twice fix-ON and twice fix-OFF; artifacts are ROM-derived local
validation data -- do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="$2"; shift 2 ;;
        --end-timer) END_TIMER="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --diagnostic-dumps) DIAGNOSTIC_DUMPS=1; shift ;;
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

if [[ ! -x "$BINARY" || ! -e "$ROM" ]]; then
    echo "streets determinism regression: SKIP (native binary or ROM absent)"
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

# One deterministic vanilla run in its OWN cwd (isolated ge007.ini). Captures the
# whole-sim state hash + a screenshot. Prints "<hash> <screenshot-md5>" on stdout.
run_state() {
    local label="$1"; shift          # unique per-process dir
    local extra_env=("$@")           # extra KEY=VAL pairs (fix-OFF opt-out)
    local dir="$OUT_DIR/$label"; mkdir -p "$dir"
    local log="$dir/log"
    local hj="$dir/hash.json"
    local rc=0
    local diag_env=()

    if [[ "$DIAGNOSTIC_DUMPS" -eq 1 ]]; then
        diag_env+=(
            GE007_SIM_HASH_PER_REGION=1
            GE007_SIM_HASH_DUMP="$dir/pool.raw.bin"
            GE007_SIM_HASH_CANON_DUMP="$dir/pool.canon.bin"
        )
    fi

    ( cd "$dir" && validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1 \
            ${diag_env[@]+"${diag_env[@]}"} \
            ${extra_env[@]+"${extra_env[@]}"} \
            "$BINARY" --rom "$ROM" --level "$LEVEL" --deterministic \
            --screenshot-game-timer "$END_TIMER" --screenshot-exit \
            --sim-state-hash-out "$hj" ) >"$log" 2>&1 || rc=$?
    echo "$rc" > "$dir/rc"
    local h shot
    h="$(grep -o '[0-9a-f]\{16\}' "$hj" 2>/dev/null | head -1)"
    shot="$(md5 -q "$dir/screenshot_000.bmp" 2>/dev/null || md5sum "$dir/screenshot_000.bmp" 2>/dev/null | cut -d' ' -f1)"
    echo "$h ${shot:-NOSHOT}"
}

fail() { echo "streets determinism regression: FAIL -- $1"; echo "artifacts: $OUT_DIR"; exit 1; }

echo "== streets determinism regression: level $LEVEL, end-timer $END_TIMER =="

read -r HASH_ON1 SHOT_ON1 <<<"$(run_state on1)"
read -r HASH_ON2 SHOT_ON2 <<<"$(run_state on2)"
read -r HASH_OFF1 SHOT_OFF1 <<<"$(run_state off1 GE007_NO_EFFECT_BUF_ZERO_INIT=1)"
read -r HASH_OFF2 SHOT_OFF2 <<<"$(run_state off2 GE007_NO_EFFECT_BUF_ZERO_INIT=1)"

# (A) every run booted and produced a hash + screenshot.
for lbl in on1 on2 off1 off2; do
    rc="$(cat "$OUT_DIR/$lbl/rc" 2>/dev/null || echo 99)"
    [[ "$rc" -eq 0 ]] || fail "$lbl process exited $rc (see $OUT_DIR/$lbl/log)"
done
[[ -n "$HASH_ON1" && -n "$HASH_ON2" && -n "$HASH_OFF1" && -n "$HASH_OFF2" ]] \
    || fail "a run produced no sim-state hash"
[[ "$SHOT_ON1" != "NOSHOT" && "$SHOT_OFF1" != "NOSHOT" ]] \
    || fail "a run produced no screenshot"
echo "  fix-ON  hashes: $HASH_ON1 / $HASH_ON2"
echo "  fix-OFF hashes: $HASH_OFF1 / $HASH_OFF2"

# (B) rendered gameplay is byte-identical across ALL four runs: deterministic and
#     unaffected by the flag (byte-identity under the opt-out).
if [[ "$SHOT_ON1" != "$SHOT_ON2" || "$SHOT_ON1" != "$SHOT_OFF1" || "$SHOT_ON1" != "$SHOT_OFF2" ]]; then
    fail "screenshots differ across runs ($SHOT_ON1/$SHOT_ON2/$SHOT_OFF1/$SHOT_OFF2) -- gameplay is NOT byte-identical; the zero-init changed observable behavior"
fi
echo "  screenshots byte-identical across all runs ($SHOT_ON1): PASS"

# (C) THE core assertion: two fix-ON processes agree (cross-process determinism).
[[ "$HASH_ON1" == "$HASH_ON2" ]] || \
    fail "fix-ON is non-deterministic across processes ($HASH_ON1 != $HASH_ON2) -- the effect-buffer zero-init is not doing its job (reverted?)"
echo "  fix-ON deterministic across processes: PASS"

# (D) fail-on-revert: without the fix the stage is non-deterministic across
#     processes (the FID-0046 defect). If this ever passes, the leftover-scratch
#     leak is gone by other means and (C) already covers determinism -- but as
#     long as the opt-out restores the legacy path, two fix-OFF runs must diverge.
[[ "$HASH_OFF1" != "$HASH_OFF2" ]] || \
    fail "fix-OFF is deterministic ($HASH_OFF1 == $HASH_OFF2) -- the opt-out no longer restores the uninitialized-read defect, so this gate cannot prove the fix is load-bearing"
echo "  fix-OFF non-deterministic across processes (fail-on-revert holds): PASS"

echo "streets determinism regression: PASS"
echo "artifacts: $OUT_DIR"
exit 0
