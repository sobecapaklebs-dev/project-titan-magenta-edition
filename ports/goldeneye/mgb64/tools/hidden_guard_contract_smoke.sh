#!/bin/bash
#
# hidden_guard_contract_smoke.sh -- regression for the chrTickBeams hidden-guard
# contract (no phantom fire / no phantom damage; H1 fire-discharge gate + H2
# AI-freeze gate).
#
# Deterministic scenario on Dam: warp a guard point-blank in front of Bond, give
# it a firing AI list so it actively shoots (firecount climbs, Bond health
# drops), then set CHRFLAG_HIDDEN on it mid-fight via GE007_AUTO_SET_CHRFLAG.
#
# Two modes, each isolating one gate (run both by default):
#   h2: hide with CHRFLAG_00040000 cleared (SET=0x400 CLR=0x40000). The H2 AI-tick
#       gate freezes the AI -> action/ai.offset frozen, and consequently no fire.
#   h1: hide with CHRFLAG_00040000 set (SET=0x40400 CLR=0). The AI keeps ticking
#       (action/ai.offset change) but the H1 gate blocks the discharge, so
#       firecount stays frozen -- isolating H1.
#
# ROM-derived captures (traces/screenshots/logs) stay local; do not commit them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=180
OUT_DIR="/tmp/mgb64_hidden_guard_contract_$$"
LEVEL=33
CHRNUM=0
WARP_FRAME=60
WARP_DISTANCE=200
FIRE_FRAME=80
AI_LIST=13
HIDE_FRAME=400
FRAMES=600
MIN_FIRES=5
MODE="both"

usage() {
    cat <<'USAGE'
Usage: tools/hidden_guard_contract_smoke.sh [options]

Options:
  --mode h1|h2|both    which gate(s) to exercise (default: both)
  --level N            raw LEVELID (default: 33 = Dam)
  --chrnum C           guard chrnum to warp/hide/trace (default: 0)
  --warp-frame N       frame to warp the guard near Bond (default: 60)
  --warp-distance D    warp distance in front of Bond (default: 200)
  --fire-frame N       frame to put the guard on the firing AI list (default: 80)
  --ai-list L          firing AI list id (default: 13)
  --hide-frame N       frame to set CHRFLAG_HIDDEN (default: 400)
  --frames N           capture/exit frame (default: 600)
  --min-fires N        min firecount the guard must reach while visible (default: 5)
  --out-dir DIR        output dir (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary (default: build/ge007)
  --build-dir DIR      CMake build dir (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-mode process timeout (default: 180)

Artifacts are ROM-derived local validation data; do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode) MODE="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --chrnum) CHRNUM="$2"; shift 2 ;;
        --warp-frame) WARP_FRAME="$2"; shift 2 ;;
        --warp-distance) WARP_DISTANCE="$2"; shift 2 ;;
        --fire-frame) FIRE_FRAME="$2"; shift 2 ;;
        --ai-list) AI_LIST="$2"; shift 2 ;;
        --hide-frame) HIDE_FRAME="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --min-fires) MIN_FIRES="$2"; shift 2 ;;
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

case "$MODE" in h1|h2|both) ;; *) echo "FAIL: --mode must be h1|h2|both" >&2; exit 2 ;; esac
for name in LEVEL WARP_FRAME WARP_DISTANCE FIRE_FRAME AI_LIST HIDE_FRAME FRAMES MIN_FIRES TIMEOUT_SECONDS; do
    v="${!name}"
    [[ "$v" =~ ^[0-9]+$ ]] || { echo "FAIL: $name must be a non-negative integer: $v" >&2; exit 2; }
done
if [[ "$HIDE_FRAME" -ge "$FRAMES" || "$FIRE_FRAME" -ge "$HIDE_FRAME" || "$WARP_FRAME" -ge "$FIRE_FRAME" ]]; then
    echo "FAIL: require WARP_FRAME < FIRE_FRAME < HIDE_FRAME < FRAMES" >&2; exit 2
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

# run_mode <h1|h2> -> 0 pass / 1 fail
run_mode() {
    local mode="$1"
    local set_mask clr_mask
    if [[ "$mode" == "h2" ]]; then set_mask="0x400"; clr_mask="0x40000"; else set_mask="0x40400"; clr_mask="0"; fi
    local dir="$OUT_DIR/$mode"; mkdir -p "$dir"
    local trace="$dir/trace.jsonl"; local log="$dir/run.log"

    echo "== [$mode] level $LEVEL chr $CHRNUM: warp@$WARP_FRAME d=$WARP_DISTANCE, fire list $AI_LIST @$FIRE_FRAME, hide@$HIDE_FRAME (SET=$set_mask CLR=$clr_mask) =="
    local rc=0
    (
        cd "$dir"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
                SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
                GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 \
                GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1 \
                GE007_TRACE_CHRNUM="$CHRNUM" \
                GE007_AUTO_WARP_CHRNUM="$CHRNUM" GE007_AUTO_WARP_CHR_FRAME="$WARP_FRAME" \
                GE007_AUTO_WARP_CHR_DISTANCE="$WARP_DISTANCE" GE007_AUTO_WARP_CHR_ANGLE=0 \
                GE007_AUTO_SET_CHR_AI_FRAME="$FIRE_FRAME" GE007_AUTO_SET_CHR_AI_CHRNUM="$CHRNUM" \
                GE007_AUTO_SET_CHR_AI_LIST="$AI_LIST" \
                GE007_AUTO_SET_CHRFLAG_FRAME="$HIDE_FRAME" GE007_AUTO_SET_CHRFLAG_CHRNUM="$CHRNUM" \
                GE007_AUTO_SET_CHRFLAG_SET="$set_mask" GE007_AUTO_SET_CHRFLAG_CLR="$clr_mask" \
                "$BINARY" --rom "$ROM" --level "$LEVEL" --deterministic \
                --trace-state "$trace" --screenshot-frame "$FRAMES" \
                --screenshot-label "hidden_guard_${mode}_$$" --screenshot-exit
    ) >"$log" 2>&1 || rc=$?

    if [[ "$rc" -eq 124 ]]; then echo "  [$mode] process: FAIL (timeout)"; tail -15 "$log" | sed 's/^/    /'; return 1
    elif [[ "$rc" -ne 0 ]]; then echo "  [$mode] process: FAIL (exit $rc)"; tail -15 "$log" | sed 's/^/    /'; return 1; fi
    echo "  [$mode] process: PASS"

    local a; a="$(grep -cF "[GEASSERT]" "$log" 2>/dev/null || true)"
    if [[ "${a:-0}" -ne 0 ]]; then echo "  [$mode] assertions: FAIL ($a)"; grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/    /'; return 1; fi
    echo "  [$mode] assertions: PASS"

    if python3 tools/audit_hidden_guard_contract.py --trace "$trace" --mode "$mode" --chrnum "$CHRNUM" --min-fires "$MIN_FIRES"; then
        return 0
    else
        return 1
    fi
}

FAILED=0
if [[ "$MODE" == "both" ]]; then
    run_mode h2 || FAILED=1
    run_mode h1 || FAILED=1
else
    run_mode "$MODE" || FAILED=1
fi

echo
if [[ "$FAILED" -eq 0 ]]; then
    echo "hidden-guard contract smoke: PASS"
    echo "artifacts: $OUT_DIR"
    exit 0
else
    echo "hidden-guard contract smoke: FAIL"
    echo "artifacts: $OUT_DIR"
    exit 1
fi
