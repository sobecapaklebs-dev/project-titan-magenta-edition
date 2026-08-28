#!/bin/bash
#
# intro_weapon_buf_fail_smoke.sh -- FID-0006 intro weapon sub-buffer overlap guard.
#
# The animated Dam intro packs Bond's body, head, and right-hand weapon into one
# shared load buffer. R9 (commit 25acc40) added a bounds guard: when the weapon
# would overrun/overlap the head mesh, it skips load_object_fill_header + the
# attach and lands Bond in the tolerated unarmed (rhandPropID<0) state instead of
# corrupting the buffer (the intro red-shard class). Stock Dam leaves ~48KB slack
# so the guard's FAIL branch is unreachable in normal play -- it is exercised ONLY
# via the GE007_BOND_WEAPON_BUF_FAIL hatch by design. The prior lane
# (intro_visual_regression.sh) drives the positive path but never the fail path,
# so a revert of the skip logic stayed green (FID-0006 self-admitted gap).
#
# This lane drives BOTH sides and asserts the guarded outcome (the T4/M1.1
# negative-control numbers):
#
#   positive (no hatch)              -> 0 RENDER-HEALTH lines, weapon HELD
#                                       (right_item_match>0), body intact, exit 0
#   GE007_BOND_WEAPON_BUF_FAIL=1     -> EXACTLY 1 [BONDVIEW][RENDER-HEALTH] line,
#                                       weapon SKIPPED (right_item_match=0), body
#                                       intact (present/rendered == positive), exit 0
#
# Body intact == the skip removes ONLY the weapon: present/rendered on the fail
# path equal the positive path exactly. Reverting the skip logic makes the hatch
# a no-op (weapon loads, right_item_match>0, 0 RENDER-HEALTH) -> this lane reddens.
#
# ROM-gated: skips cleanly (exit 0) without a ROM. Captured traces/logs/saves are
# ROM-derived local artifacts -- do not commit them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=240
FRAME=900
LEVEL=33
# PP7 is the intro right-hand item on Dam (level 33); the positive path holds it
# (right_item_match>0), the guarded fail path skips it (right_item_match=0).
REQUIRE_RIGHT_ITEM=5
OUT_DIR="/tmp/mgb64_intro_weapon_buf_fail_$$"

usage() {
    cat <<'USAGE'
Usage: tools/intro_weapon_buf_fail_smoke.sh [options]

Options:
  --out-dir DIR      output directory (default: /tmp/...)
  --rom PATH         ROM path (default: ./baserom.u.z64)
  --binary PATH      native binary path (default: build/ge007)
  --build-dir DIR    CMake build directory (default: build)
  --no-build         reuse an existing native binary
  --frame N          deterministic screenshot/exit frame (default: 900)
  --timeout SECONDS  per-capture timeout (default: 240)

Captured traces, logs, and saves are ROM-derived local artifacts. Do not commit.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --frame) FRAME="$2"; shift 2 ;;
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

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: intro_weapon_buf_fail: ROM not found ($ROM); ROM-gated lane."
    exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi
validation_require_binary "$BINARY"
validation_acquire_runtime_lock
trap 'validation_release_runtime_lock; rm -rf "$OUT_DIR"; rm -f screenshot_introwbf.bmp' EXIT INT TERM

mkdir -p "$OUT_DIR"

# capture <name> [extra env assignments...]
capture() {
    local name="$1"; shift
    local d="$OUT_DIR/$name"
    mkdir -p "$d/save"
    rm -f screenshot_introwbf.bmp
    if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
            GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=5 \
            "$@" \
            "$BINARY" \
            --savedir "$d/save" --rom "$ROM" --level "$LEVEL" --deterministic \
            --trace-state "$d/trace.jsonl" \
            --screenshot-frame "$FRAME" --screenshot-label introwbf \
            --screenshot-exit >"$d/run.log" 2>&1; then
        echo "FAIL: capture $name failed (nonzero exit / crash)"
        tail -20 "$d/run.log" | sed 's/^/  /'
        return 1
    fi
    rm -f screenshot_introwbf.bmp
    if grep -qF "[GEASSERT]" "$d/run.log"; then
        echo "FAIL: GEASSERT fired during $name"
        grep -F "[GEASSERT]" "$d/run.log" | head -5 | sed 's/^/  /'
        return 1
    fi
    if [[ ! -s "$d/trace.jsonl" ]]; then
        echo "FAIL: capture $name produced no state trace"
        return 1
    fi
    # count the guard's one-shot RENDER-HEALTH line
    grep -c "\[BONDVIEW\]\[RENDER-HEALTH\]" "$d/run.log" || true
}

# audit <name> -> writes $OUT_DIR/<name>.json (audit_intro_trace metrics)
audit() {
    local name="$1"
    local d="$OUT_DIR/$name"
    python3 tools/audit_intro_trace.py "$d/trace.jsonl" \
        --label "$name" --require-right-item "$REQUIRE_RIGHT_ITEM" \
        --json-out "$OUT_DIR/$name.json" >/dev/null 2>&1 || true
}

# read a nested counts field from the audit JSON
count_field() {  # count_field <name> <field>
    python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['counts'][sys.argv[2]])" \
        "$OUT_DIR/$1.json" "$2"
}

echo "=== intro_weapon_buf_fail (Dam intro, level $LEVEL, frame $FRAME) ==="

rc=0

# --- positive path: weapon held, no guard fires ---
pos_health="$(capture positive)"
audit positive
pos_present="$(count_field positive present)"
pos_rendered="$(count_field positive rendered)"
pos_match="$(count_field positive right_item_match)"
echo "  positive : RENDER-HEALTH=$pos_health present=$pos_present rendered=$pos_rendered right_item_match=$pos_match"

if [[ "$pos_health" -ne 0 ]]; then
    echo "FAIL: positive path emitted $pos_health RENDER-HEALTH line(s) (expected 0)"; rc=1
fi
if [[ "$pos_match" -le 0 ]]; then
    echo "FAIL: positive path did not hold the intro weapon (right_item_match=$pos_match, expected >0)"; rc=1
fi
if [[ "$pos_rendered" -le 0 ]]; then
    echo "FAIL: positive path body did not render (rendered=$pos_rendered)"; rc=1
fi

# --- fail path: forced guard fires, weapon skipped, body intact ---
fail_health="$(capture failpath GE007_BOND_WEAPON_BUF_FAIL=1)"
audit failpath
fail_present="$(count_field failpath present)"
fail_rendered="$(count_field failpath rendered)"
fail_match="$(count_field failpath right_item_match)"
echo "  failpath : RENDER-HEALTH=$fail_health present=$fail_present rendered=$fail_rendered right_item_match=$fail_match"

if [[ "$fail_health" -ne 1 ]]; then
    echo "FAIL: fail path emitted $fail_health RENDER-HEALTH line(s) (expected EXACTLY 1)"; rc=1
fi
if [[ "$fail_match" -ne 0 ]]; then
    echo "FAIL: fail path still held the weapon (right_item_match=$fail_match, expected 0 -- guard did not skip)"; rc=1
fi
if [[ "$fail_rendered" -le 0 ]]; then
    echo "FAIL: fail path body did not render (rendered=$fail_rendered) -- guard corrupted the body"; rc=1
fi
# body intact == the skip removed ONLY the weapon: fail-path body counts equal positive.
if [[ "$fail_present" -ne "$pos_present" || "$fail_rendered" -ne "$pos_rendered" ]]; then
    echo "FAIL: fail-path body counts (present=$fail_present rendered=$fail_rendered) differ from positive (present=$pos_present rendered=$pos_rendered) -- the guard should skip ONLY the weapon, leaving the body intact"; rc=1
fi

if [[ "$rc" -ne 0 ]]; then
    echo "FAIL: intro_weapon_buf_fail"
    exit 1
fi
echo "PASS: intro_weapon_buf_fail (guard skips only the weapon; body intact ${pos_rendered}==${fail_rendered}; exactly 1 RENDER-HEALTH on fail, 0 on positive)"
