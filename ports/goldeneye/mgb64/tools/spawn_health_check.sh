#!/bin/bash
#
# spawn_health_check.sh -- Verify spawn-time invariants for GoldenEye PC port.
#
# Checks per-level (using GE007_DEBUG diagnostics):
#   1. field_70 is seeded (non-zero) at camera handoff
#   2. standheight is initialized to a sane value
#   3. No GEASSERT failures
#   4. Guard CHR_RENDER lines present (guards visible)
#   5. No crashes (clean exit)
#   6. Render-health counters stay clean in the per-frame JSON trace
#
# Usage:
#   ./tools/spawn_health_check.sh              # test Dam + Cradle
#   ./tools/spawn_health_check.sh --all        # test all 20 levels
#   ./tools/spawn_health_check.sh --level 33   # test single level
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
FRAME=60
DO_BUILD=1
TIMEOUT_SECONDS=30
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"
# Dam(33) and Cradle(41) are the primary regression targets
LEVELS="33 41"
ALL_LEVELS="33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32"
# Frigate's deterministic direct-boot spawn camera has no guard in view during
# the first 60-frame smoke window, even though the level boots and renders.
NO_EARLY_GUARD_RENDER_LEVELS="26"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --all) LEVELS="$ALL_LEVELS"; shift ;;
        --level) LEVELS="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 2 ;;
    esac
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    if [[ ! -x "$BINARY" ]]; then
        echo "Binary not found, building..."
    fi
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_acquire_runtime_lock
SCREENSHOT_TMPDIR=""

FAILED=0
PASSED=0
TOTAL=0

cleanup() {
    if [[ -n "$SCREENSHOT_TMPDIR" ]]; then
        rm -rf "$SCREENSHOT_TMPDIR"
    fi
    validation_release_runtime_lock
}
trap cleanup EXIT INT TERM

SCREENSHOT_TMPDIR="$(mktemp -d /tmp/ge007_spawn_screens.XXXXXX)"

level_in_list() {
    local needle="$1"
    local item
    shift
    for item in "$@"; do
        if [[ "$item" == "$needle" ]]; then
            return 0
        fi
    done
    return 1
}

for lvl in $LEVELS; do
    echo ""
    echo "=== Spawn Health: Level $lvl ==="
    TOTAL=$((TOTAL + 1))
    LOGFILE="/tmp/ge007_spawn_${lvl}_$$.log"
    SCREENSHOT_LABEL="spawn_${lvl}_$$"
    TRACE="$SCREENSHOT_TMPDIR/trace_${lvl}.jsonl"
    rm -f "${SCREENSHOT_TMPDIR}/screenshot_${SCREENSHOT_LABEL}.bmp"
    rm -f "$TRACE"

    # Run with GE007_DEBUG to get GEDBG output on stderr
    GAME_EXIT=0
    if [[ -n "$TIMEOUT_BIN" ]]; then
        (
            cd "$SCREENSHOT_TMPDIR"
            env -u GE007_DEBUG SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DEBUG=1 GE007_ASSERT_ON_FAIL=0 \
                "$TIMEOUT_BIN" --kill-after=5 "$TIMEOUT_SECONDS" "$BINARY" \
                --rom "$ROM" \
                --level "$lvl" --deterministic \
                --trace-state "$TRACE" \
                --screenshot-frame "$FRAME" --screenshot-label "$SCREENSHOT_LABEL" --screenshot-exit
        ) \
            >"$LOGFILE" 2>&1 || GAME_EXIT=$?
    else
        (
            cd "$SCREENSHOT_TMPDIR"
            env -u GE007_DEBUG SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DEBUG=1 GE007_ASSERT_ON_FAIL=0 \
                "$BINARY" \
                --rom "$ROM" \
                --level "$lvl" --deterministic \
                --trace-state "$TRACE" \
                --screenshot-frame "$FRAME" --screenshot-label "$SCREENSHOT_LABEL" --screenshot-exit
        ) \
            >"$LOGFILE" 2>&1 || GAME_EXIT=$?
    fi

    LEVEL_FAIL=0

    # Check 1: Process didn't crash or timeout
    if [[ "$GAME_EXIT" -ne 0 ]]; then
        if [[ "$GAME_EXIT" -eq 124 ]]; then
            echo "  FAIL: game timed out (hung)"
        else
            echo "  FAIL: game exited with code $GAME_EXIT"
        fi
        LEVEL_FAIL=1
    else
        echo "  process: PASS (clean exit)"
    fi

    # Check 2: field_70 seeded at camera handoff
    HANDOFF=$(grep "CAMERA_HANDOFF" "$LOGFILE" | head -1)
    if [[ -n "$HANDOFF" ]]; then
        F70=$(echo "$HANDOFF" | grep -oE 'field_70=[0-9.eE+-]+' | head -1 | cut -d= -f2)
        if [[ -n "$F70" ]] && python3 -c "import sys; sys.exit(0 if float('$F70') != 0.0 else 1)" 2>/dev/null; then
            echo "  field_70: PASS ($F70)"
        else
            echo "  FAIL: field_70 is zero at camera handoff"
            LEVEL_FAIL=1
        fi
    else
        echo "  field_70: SKIP (no CAMERA_HANDOFF line — level may use different spawn path)"
    fi

    # Check 3: standheight initialized
    SH_LINE=$(grep "INIT_STANDHEIGHT" "$LOGFILE" | head -1)
    if [[ -n "$SH_LINE" ]]; then
        SH_VAL=$(echo "$SH_LINE" | grep -oE 'INIT_STANDHEIGHT=[0-9.eE+-]+' | head -1 | cut -d= -f2)
        if [[ -n "$SH_VAL" ]] && python3 -c "
import sys
v = float('$SH_VAL')
sys.exit(0 if -50000 < v < 50000 and v != 0 else 1)
" 2>/dev/null; then
            echo "  standheight: PASS ($SH_VAL)"
        else
            echo "  FAIL: standheight out of range or zero ($SH_VAL)"
            LEVEL_FAIL=1
        fi
    else
        echo "  standheight: SKIP (no INIT_STANDHEIGHT line)"
    fi

    # Check 4: No GEASSERT failures
    ASSERT_COUNT=$(grep -cF "[GEASSERT]" "$LOGFILE" 2>/dev/null || true)
    ASSERT_COUNT="${ASSERT_COUNT:-0}"
    if [[ "$ASSERT_COUNT" -eq 0 ]]; then
        echo "  assertions: PASS (0 failures)"
    else
        echo "  FAIL: $ASSERT_COUNT assertion(s) fired"
        grep -F "[GEASSERT]" "$LOGFILE" | head -5 | sed 's/^/    /'
        LEVEL_FAIL=1
    fi

    # Check 5: Per-frame renderer health trace
    if [[ ! -s "$TRACE" ]]; then
        echo "  FAIL: render trace missing or empty"
        LEVEL_FAIL=1
    elif python3 tools/audit_render_trace.py --label "spawn level $lvl" "$TRACE" >/tmp/ge007_spawn_render_${lvl}_$$.log 2>&1; then
        echo "  render_health: PASS"
    else
        echo "  FAIL: render health audit failed"
        sed -n '1,12p' "/tmp/ge007_spawn_render_${lvl}_$$.log" | sed 's/^/    /'
        LEVEL_FAIL=1
    fi
    rm -f "/tmp/ge007_spawn_render_${lvl}_$$.log"

    # Check 6: Guard rendering present (CHR_RENDER lines)
    CHR_COUNT=$(grep -cF "CHR_RENDER" "$LOGFILE" 2>/dev/null || true)
    CHR_COUNT="${CHR_COUNT:-0}"
    if [[ "$CHR_COUNT" -gt 0 ]]; then
        echo "  guard_render: PASS ($CHR_COUNT CHR_RENDER calls)"
    elif level_in_list "$lvl" $NO_EARLY_GUARD_RENDER_LEVELS; then
        echo "  guard_render: SKIP (no guard in deterministic first-view window)"
    else
        echo "  guard_render: WARN (0 CHR_RENDER — may be expected if no guards in view)"
    fi

    rm -f "$LOGFILE"

    if [[ "$LEVEL_FAIL" -eq 1 ]]; then
        FAILED=$((FAILED + 1))
    else
        PASSED=$((PASSED + 1))
    fi
done

echo ""
echo "=== Spawn Health: $PASSED/$TOTAL passed, $FAILED failed ==="
exit "$FAILED"
