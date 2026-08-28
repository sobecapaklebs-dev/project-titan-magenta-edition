#!/bin/bash
#
# ammo_crate_collect_smoke.sh -- Guard multi-ammo crate (PROPDEF_AMMO) collection.
#
# Regression gate for the ammo-crate collect stride. The retail collect loop
# (interact_ammobox_object, asm 7F050338) walks the 13 {u16 modelnum,
# u16 quantity} authored pairs with a 4-byte stride and reads slots[i].quantity,
# mapping slot i to ammotype i+1 (slot 1 shares the 9mm pool). A prior port
# regression read the smaller quantities[] overlay (2-byte stride) with ammotype
# i, which for odd i lands on a modelnum lane -- 0xFFFF for "no ammo" slots --
# and maxed out several ammo types on pickup (e.g. every Runway crate maxing
# 9mm/rifle/grenade/remote/timed/grenade-round).
#
# This smoke direct-boots Runway, warps Bond onto a crate pad, and asserts via
# GE007_INTERACT_TRACE that the collect dispenses only the small authored
# quantities -- never a modelnum-lane blowout (amount >= 1000) -- and that the
# ammotype mapping is the faithful i+1 sequence (two type=1 entries for slots
# 0/1, then 3..13).
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_ammo_crate_collect_$$"
LEVEL=runway
PAD=3
WARP_FRAME=60
FRAMES=240
# A modelnum lane read as a quantity is either 0xFFFF (65535, "none") or a real
# model index (hundreds); authored ammo quantities are single/low-double digits
# even after the solo multiplier. 1000 cleanly separates the two.
MAXOUT_THRESHOLD=1000

usage() {
    cat <<'USAGE'
Usage: tools/ammo_crate_collect_smoke.sh [options]

Options:
  --out-dir DIR          output directory (default: /tmp/...)
  --level NAME|N         level slug or id with an ammo crate (default: runway)
  --pad N                pad index of the crate to warp onto (default: 3)
  --warp-frame N         frame at which to warp Bond onto the pad (default: 60)
  --frames N             deterministic exit frame (default: 240)
  --rom PATH             ROM path (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --timeout SECONDS      capture timeout (default: 120)

Artifacts are ROM-derived local validation data. Do not commit captured traces
or logs.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --pad) PAD="$2"; shift 2 ;;
        --warp-frame) WARP_FRAME="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for pair in \
    "pad:$PAD" \
    "warp-frame:$WARP_FRAME" \
    "frames:$FRAMES" \
    "timeout:$TIMEOUT_SECONDS"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: --$name must be a non-negative integer: $value" >&2
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
validation_acquire_runtime_lock

cleanup() {
    validation_release_runtime_lock
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

LOG="$OUT_DIR/run.log"
SAVE_DIR="$OUT_DIR/save"
rm -f "$LOG"
mkdir -p "$SAVE_DIR"

echo "=== Ammo Crate Collect Smoke ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL"
echo "  pad:     $PAD (warp frame $WARP_FRAME)"
echo "  frames:  $FRAMES"

if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
    env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1 \
        GE007_ASSERT_ON_FAIL=0 \
        GE007_DISABLE_LEVEL_INTRO=1 \
        GE007_INTERACT_TRACE=1 \
        GE007_INTERACT_TRACE_BUDGET=2000 \
        GE007_AUTO_WARP_PAD="$PAD" \
        GE007_AUTO_WARP_FRAME="$WARP_FRAME" \
        GE007_AUTO_EXIT_FRAME="$FRAMES" \
        "$BINARY" \
        --savedir "$SAVE_DIR" \
        --rom "$ROM" \
        --level "$LEVEL" \
        --deterministic >"$LOG" 2>&1; then
    echo "FAIL: ammo crate collect capture failed" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

if grep -qF "[GEASSERT]" "$LOG"; then
    echo "FAIL: GEASSERT fired during ammo crate collect capture" >&2
    grep -F "[GEASSERT]" "$LOG" | head -5 | sed 's/^/  /' >&2
    exit 1
fi
if ! grep -qF "deterministic frame exit observed" "$LOG"; then
    echo "FAIL: deterministic frame-exit marker was not observed" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

python3 - "$LOG" "$MAXOUT_THRESHOLD" <<'PY'
import re
import sys

log_path, threshold = sys.argv[1], int(sys.argv[2])
with open(log_path, "r", errors="replace") as f:
    lines = f.readlines()

# Find the PROPDEF_AMMO (objtype=20) collect block and gather its add_ammo calls.
in_ammo = False
collected = False
calls = []  # (ammotype, amount)
begin_re = re.compile(r"collect begin .*objtype=(\d+)")
add_re = re.compile(r"add_ammo type=(-?\d+) amount=(-?\d+)")
free_re = re.compile(r"collect free objtype=(\d+)")

for ln in lines:
    if "[INTERACT_TRACE]" not in ln:
        continue
    m = begin_re.search(ln)
    if m:
        in_ammo = (m.group(1) == "20")
        if in_ammo:
            calls = []
        continue
    m = free_re.search(ln)
    if m and m.group(1) == "20" and in_ammo:
        collected = True
        in_ammo = False
        break
    if in_ammo:
        m = add_re.search(ln)
        if m:
            calls.append((int(m.group(1)), int(m.group(2))))

fail = []
if not collected:
    fail.append("no PROPDEF_AMMO (objtype=20) collect was observed -- crate not "
                "picked up (check --pad/--warp-frame/--level)")
else:
    # Retail loop makes 13 add_ammo calls, one per authored slot.
    if len(calls) != 13:
        fail.append(f"expected 13 add_ammo calls, saw {len(calls)}: {calls}")

    amounts = [a for _, a in calls]
    ammotypes = [t for t, _ in calls]

    # Max-out detection: no authored quantity (even x solo multiplier) approaches
    # the threshold; a modelnum-lane read (0xFFFF or a model index) blows past it.
    blown = [(t, a) for t, a in calls if a >= threshold]
    if blown:
        fail.append(f"max-out detected -- add_ammo amount >= {threshold}: {blown} "
                    "(collect path is reading the quantities[] overlay / modelnum lanes)")

    # Ammotype mapping must be the faithful i+1 sequence: slots 0 and 1 both map
    # to ammotype 1 (9mm pool), then 3,4,...,13. Ammotype 0 (NONE) and 2 (9MM_2)
    # must never be targeted.
    expected_types = [1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
    if ammotypes and ammotypes != expected_types:
        fail.append(f"ammotype mapping wrong: expected {expected_types}, saw {ammotypes}")

    # The authored ammo must actually land: at least one slot dispensed a
    # positive, sane quantity.
    positive = [(t, a) for t, a in calls if a > 0]
    if not positive:
        fail.append("no positive ammo dispensed -- authored quantity did not land")

if fail:
    print("AMMO CRATE COLLECT SMOKE: FAIL")
    for f in fail:
        print(f"  - {f}")
    sys.exit(1)

print("AMMO CRATE COLLECT SMOKE: PASS")
print(f"  add_ammo calls (type, amount): {calls}")
PY
rc=$?
if [[ $rc -ne 0 ]]; then
    echo "FAIL: ammo crate collect assertions failed" >&2
    echo "  --- add_ammo trace ---" >&2
    grep -F "add_ammo" "$LOG" | sed 's/^/  /' >&2 || true
    exit 1
fi

echo "PASS: ammo crate collect dispensed only authored quantities (no max-out)"
