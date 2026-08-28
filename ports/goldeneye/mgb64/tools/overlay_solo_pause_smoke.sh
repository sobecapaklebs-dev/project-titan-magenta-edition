#!/bin/bash
#
# overlay_solo_pause_smoke.sh -- MC.7 proof: the in-game settings overlay pauses
# the SIM in single-player and resumes cleanly, without tripping the stall
# watchdog. This is a real-time (NON-deterministic) run: the pause is a
# wall-clock concept and never engages under --deterministic (see
# portOverlaySoloPauseActive in src/game/lvl.c).
#
# How it works
# ------------
#   * build/ge007 links the app shell (src/app/main_app.cpp), so MGB64_APP_AUTOPLAY
#     boots a solo level with the F1/View overlay installed (Overlay_install).
#   * MGB64_TEST_OVERLAY_OPEN_FRAME / _CLOSE_FRAME (src/app/ui_overlay.cpp) script
#     the overlay open/close by rendered-frame ordinal, so no human is needed.
#   * GE007_TRACE_SOLO_PAUSE=1 (src/game/lvl.c) prints g_GlobalTimer at each
#     pause ENGAGED / RELEASED edge. Because g_ClockTimer is forced to 0 while
#     paused, g_GlobalTimer (which accumulates g_ClockTimer) MUST be identical at
#     ENGAGED and RELEASED -- that equality is the freeze proof. It must climb
#     again afterwards (resume proof).
#   * MGB64_BOOT_SCREENSHOT_FRAME bounds the run and exits cleanly.
#   * The stall watchdog (src/platform/stall_watchdog.c) is suppressed while
#     paused (portWatchdogSetPaused); we assert it wrote no stall block.
#
# Captures are ROM-derived and must stay local; do not commit logs/screenshots.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_overlay_solo_pause_smoke_$$"
LEVEL="dam"
OPEN_FRAME=120        # open the overlay after the level is live
PAUSE_FRAMES=660      # ~11s at 60fps: exceeds the 5s watchdog stall threshold
EXIT_MARGIN=120       # render this many frames after close, then screenshot+exit

usage() {
    cat <<'USAGE'
Usage: tools/overlay_solo_pause_smoke.sh [options]

Options:
  --level SLUG        solo level slug to boot (default: dam)
  --open-frame N      frame to open the overlay (default: 120)
  --pause-frames N    frames to stay paused; >300 exceeds the 5s watchdog
                      threshold so a suppression regression would dump (default: 660)
  --exit-margin N     frames to render after close before screenshot+exit (default: 120)
  --out-dir DIR       output directory (default: /tmp/...)
  --rom PATH          ROM path (default: ./baserom.u.z64)
  --binary PATH       app binary path (default: build/ge007)
  --build-dir DIR     CMake build directory (default: build)
  --no-build          reuse an existing binary
  --timeout SECONDS   run timeout (default: 120)

Artifacts are ROM-derived local validation data. Do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="$2"; shift 2 ;;
        --open-frame) OPEN_FRAME="$2"; shift 2 ;;
        --pause-frames) PAUSE_FRAMES="$2"; shift 2 ;;
        --exit-margin) EXIT_MARGIN="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage; exit 2 ;;
    esac
done

CLOSE_FRAME=$(( OPEN_FRAME + PAUSE_FRAMES ))
EXIT_FRAME=$(( CLOSE_FRAME + EXIT_MARGIN ))

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR"
    validation_build "$BUILD_DIR"
fi
validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

mkdir -p "$OUT_DIR"
SAVEDIR="$OUT_DIR/save"
mkdir -p "$SAVEDIR"
STDERR_LOG="$OUT_DIR/run.stderr.log"

# The app-autoplay boot auto-detects the ROM from the working directory, so run
# from a scratch dir holding a symlink to the user's ROM under the conventional
# name. Keeps the smoke ROM-path-agnostic.
RUN_CWD="$OUT_DIR/cwd"
mkdir -p "$RUN_CWD"
ln -sf "$ROM" "$RUN_CWD/baserom.u.z64"

echo "[overlay-pause-smoke] level=$LEVEL open=$OPEN_FRAME close=$CLOSE_FRAME exit=$EXIT_FRAME"

set +e
( cd "$RUN_CWD" && \
  SDL_AUDIODRIVER=dummy GE007_MUTE=1 \
  MGB64_APP_AUTOPLAY=1 \
  MGB64_APP_AUTOPLAY_LEVEL="$LEVEL" \
  MGB64_APP_SAVEDIR="$SAVEDIR" \
  MGB64_TEST_OVERLAY_OPEN_FRAME="$OPEN_FRAME" \
  MGB64_TEST_OVERLAY_CLOSE_FRAME="$CLOSE_FRAME" \
  MGB64_BOOT_SCREENSHOT_FRAME="$EXIT_FRAME" \
  GE007_TRACE_SOLO_PAUSE=1 \
  validation_run_with_timeout "$TIMEOUT_SECONDS" "$BINARY" ) \
  >"$OUT_DIR/run.stdout.log" 2>"$STDERR_LOG"
RUN_RC=$?
set -e

echo "[overlay-pause-smoke] exit rc=$RUN_RC (stderr: $STDERR_LOG)"

fail() { echo "FAIL: $*" >&2; exit 1; }

# 1) The overlay actually opened and closed on schedule.
grep -q "\[overlay-test\] opened at frame $OPEN_FRAME" "$STDERR_LOG" \
    || fail "overlay did not open at frame $OPEN_FRAME (test hook not firing?)"
grep -q "\[overlay-test\] closed at frame $CLOSE_FRAME" "$STDERR_LOG" \
    || fail "overlay did not close at frame $CLOSE_FRAME"

# 2) The pause engaged and released, and the sim clock was FROZEN across it.
ENGAGED=$(grep -oE "\[MC7\] solo-pause ENGAGED g_GlobalTimer=[0-9-]+" "$STDERR_LOG" | tail -1 | grep -oE '[0-9-]+$' || true)
RELEASED=$(grep -oE "\[MC7\] solo-pause RELEASED g_GlobalTimer=[0-9-]+" "$STDERR_LOG" | tail -1 | grep -oE '[0-9-]+$' || true)
[[ -n "$ENGAGED"  ]] || fail "no 'solo-pause ENGAGED' marker (sim did not pause on overlay open)"
[[ -n "$RELEASED" ]] || fail "no 'solo-pause RELEASED' marker (pause never lifted)"
[[ "$ENGAGED" == "$RELEASED" ]] \
    || fail "sim clock advanced while paused: g_GlobalTimer $ENGAGED -> $RELEASED (expected equal)"
echo "[overlay-pause-smoke] PASS pause froze the sim: g_GlobalTimer stayed $ENGAGED across the whole pause"

# 3) The stall watchdog must NOT have fired during the >5s pause.
STALL_LOG="$SAVEDIR/stall_watchdog.log"
if [[ -s "$STALL_LOG" ]] && grep -q "SIM STALL" "$STALL_LOG"; then
    echo "----- $STALL_LOG -----" >&2
    cat "$STALL_LOG" >&2
    fail "stall watchdog fired during a legitimate pause (suppression regressed)"
fi
echo "[overlay-pause-smoke] PASS stall watchdog stayed silent through the ${PAUSE_FRAMES}-frame pause"

echo "[overlay-pause-smoke] OK"
