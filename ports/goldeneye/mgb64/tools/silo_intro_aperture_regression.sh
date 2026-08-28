#!/bin/bash
#
# silo_intro_aperture_regression.sh -- Guard FID-0009: Silo intro-swirl aperture leak.
#
# During the Silo level-intro SWIRL camera the detached camera orbits inside Bond's
# own room (room_pointer resolves to g_BgCurrentRoom), so the T13b draw-only
# camera-seed walk was DORMANT (its camera_seed_room stayed -1) and the far silo-wall
# room the sightline grazes (room 27 at the offending pose) dropped to the sky-blue
# clear color -- a visible "sky through the silo wall" aperture leak in a playthrough.
#
# The fix (bg.c, FID-0009) seeds the DRAW-ONLY walk from the camera's resolved room for
# EVERY detached authored-camera frame -- including the same-room swirl -- so the walk's
# additive visibility pass admits the grazing room to the DRAW list only, never to the
# sim-visible room_rendered set. This lane proves, at the offending swirl pose:
#   1. default (fix on): the aperture renders GEOMETRY -- blue-sky pixels collapse to ~0
#      and the grazing room is admitted as draw-only (in the draw list, NOT rendered).
#   2. sim purity: every sim-consumed field is BYTE-IDENTICAL, frame-for-frame, with the
#      fix on vs the GE007_NO_CAMERA_SEED_MULTIHOP opt-out -- the room_rendered read-back
#      (the FID-0012 render->sim field auto-aim/AI read), the RNG call count, the onscreen
#      actor count, and the rendered-room count. (The whole-struct g_BgRoomInfo hash is a
#      deliberately BROADER probe covering every byte of the struct. portals_to_room_count
#      -- per-frame BFS scratch with no sim consumer -- is now snapshotted/restored by the
#      walk alongside room_rendered/room_neighbor_to_rendered so it carries no phantom
#      noise either. The broader hash can still legitimately differ fix-vs-opt-out on
#      frames where the draw-only widening runs: field_36/mtxid, the room's render
#      matrix-buffer slot (src/game/unk_0BC530.c), is assigned only to actually-DRAWN
#      rooms and is itself render-only with no sim consumer -- same "renders more rooms"
#      category as the draw list/g_BgNumberOfRoomsDrawn, just struct-colocated. It is
#      intentionally NOT restored: unlike portals_to_room_count it is load-bearing for
#      the display list already queued this frame, so stomping it back would risk a real
#      rendering regression for zero sim benefit. The sim-consumed field tuple below is
#      therefore the authoritative purity oracle for this lane, not the whole-struct hash.)
#   3. fail-on-revert: with GE007_NO_CAMERA_SEED_MULTIHOP=1 (today's single-hop T13b
#      behavior) the aperture leaks again -- blue-sky pixels return and the grazing room
#      is absent -- so reverting the fix reddens this lane.
#
# ROM-gated: SKIPs cleanly without the ROM or Pillow. Artifacts are ROM-derived local
# validation data -- do not commit captured screenshots, traces, or logs.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_silo_intro_aperture_$$"
FRAME=730                 # a swirl pose framing the grazing far-wall aperture
LEVEL=20                  # LEVELID_SILO
LEAK_ROOM=27              # grazing far-wall room dropped by the dormant walk
BLUE_LEAK_MIN=5000        # opt-out (leak) must exceed this many sky-clear px
BLUE_OK_MAX=500           # default (fix) must stay below this many sky-clear px

usage() {
    cat <<'USAGE'
Usage: tools/silo_intro_aperture_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frame N            swirl screenshot/exit frame (default: 730)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-capture timeout (default: 120)
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --frame) FRAME="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! python3 -c 'import PIL' >/dev/null 2>&1; then
    echo "SKIP: silo_intro_aperture_regression: Pillow not installed (pip install pillow)."
    exit 0
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: silo_intro_aperture_regression: ROM not found ($ROM); ROM-gated lane."
    exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_acquire_runtime_lock
cleanup() { validation_release_runtime_lock; }
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

# capture <name> [extra env assignments...] -> writes screenshot + trace + hash
capture() {
    local name="$1"; shift
    local d="$OUT_DIR/$name"
    mkdir -p "$d/save"
    if ! (
        cd "$d"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
            SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
            GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=0 \
            GE007_TRACE_FULL_ROOM_LIST=1 \
            "$@" \
            "$BINARY" \
            --savedir "$d/save" --rom "$ROM" --level "$LEVEL" --deterministic \
            --trace-state "$d/trace.jsonl" \
            --screenshot-frame "$FRAME" --screenshot-label "$name" \
            --screenshot-exit
    ) >"$d/run.log" 2>&1; then
        echo "FAIL: capture $name failed" >&2
        tail -30 "$d/run.log" | sed 's/^/  /' >&2
        exit 1
    fi
    # screenshot lands in the capture CWD as screenshot_<name>.bmp
    local bmp="$d/screenshot_${name}.bmp"
    if [[ ! -f "$bmp" ]]; then
        echo "FAIL: capture $name produced no screenshot" >&2
        exit 1
    fi
    python3 -c "from PIL import Image; Image.open('$bmp').convert('RGB').save('$d/f.png')"
    if [[ ! -s "$d/trace.jsonl" ]]; then
        echo "FAIL: capture $name produced no trace" >&2
        exit 1
    fi
}

echo "=== Silo Intro Aperture Regression (FID-0009, level $LEVEL, swirl frame $FRAME) ==="
echo "  binary: $BINARY"
echo "  rom:    $ROM"

# Isolate the FID-0009 camera-seed walk that THIS lane guards. DAM-R2 added a second,
# independent draw-only admission mechanism (GE007_NO_INTRO_FARVISTA_ADMIT / bg.c far-vista
# pass); when it ran on every frozen intro it independently admitted the grazing room 27
# on Silo, MASKING the leak in the opt-out control and defeating the FID-0009 fail-on-
# revert. The pass is now scoped to the Dam level only (review round 1), so it cannot fire
# here -- but keep the opt-out pinned in BOTH captures as defense-in-depth: this lane must
# stay valid even if the far-vista pass is later extended to more levels after their own
# stock adjudication (see DAM_RENDER_DEEP_DIVE_2026-07-18.md R-01).
capture fix    GE007_NO_INTRO_FARVISTA_ADMIT=1
capture optout GE007_NO_INTRO_FARVISTA_ADMIT=1 GE007_NO_CAMERA_SEED_MULTIHOP=1

python3 - "$OUT_DIR" "$LEAK_ROOM" "$BLUE_LEAK_MIN" "$BLUE_OK_MAX" <<'PY'
import json, sys
from pathlib import Path
from collections import Counter
from PIL import Image

root = Path(sys.argv[1]); leak_room = int(sys.argv[2])
blue_leak_min = int(sys.argv[3]); blue_ok_max = int(sys.argv[4])

def blue_sky_px(png):
    im = Image.open(png).convert("RGB")
    c = Counter(im.getdata())
    # GoldenEye sky/fog clear color reads as a saturated deep blue (0,24,~93):
    # low red, low green, high blue. Count that band as leaked sky.
    return sum(v for k, v in c.items() if k[2] > 60 and k[0] < 40 and k[1] < 50)

def all_traces(name):
    recs = []
    for line in (root / name / "trace.jsonl").open():
        line = line.strip()
        if line:
            recs.append(json.loads(line))
    if not recs:
        raise SystemExit(f"FAIL: no trace records for {name}")
    return recs

def room_sets(rec):
    vis = rec["rooms"]["vis"]
    draw = set(vis.get("draw_sample") or [])
    rendered = set(vis.get("sample") or [])
    return draw, rendered

def sim_fields(rec):
    """Every field the SIM consumes (must match render-OFF vs -ON): the room_rendered
    read-back set (FID-0012), the rendered-room count, the room_neighbor_to_rendered
    count (getROOMID_isNeighborToRendered -> chrobjhandler.c:47737 door/obj/weapon
    prop visibility -- also restored by the walk, bg.c:17039), the deterministic RNG
    call count, and the onscreen actor count. Deliberately excludes the DRAW list."""
    vis = rec["rooms"]["vis"]
    return (
        tuple(sorted(vis.get("sample") or [])),
        vis.get("rendered"),
        vis.get("neighbor"),
        rec["rng"]["call_count"],
        rec["actors"]["onscreen"],
    )

fix_all = all_traces("fix"); opt_all = all_traces("optout")
fix = fix_all[-1]; optout = opt_all[-1]
fix_draw, fix_rendered = room_sets(fix)
opt_draw, opt_rendered = room_sets(optout)
fix_blue = blue_sky_px(root / "fix" / "f.png")
opt_blue = blue_sky_px(root / "optout" / "f.png")

fail = 0
print(f"  fix    : blue_sky_px={fix_blue} draw={sorted(fix_draw)} rendered={sorted(fix_rendered)}")
print(f"  optout : blue_sky_px={opt_blue} draw={sorted(opt_draw)} rendered={sorted(opt_rendered)}")

# 1. The fix closes the aperture: grazing room admitted DRAW-ONLY, sky pixels collapse.
if leak_room not in fix_draw:
    print(f"  FAIL: fix must admit grazing room {leak_room} to the draw list"); fail = 1
if leak_room in fix_rendered:
    print(f"  FAIL: grazing room {leak_room} must be DRAW-ONLY (not sim-rendered) under the fix"); fail = 1
if fix_blue > blue_ok_max:
    print(f"  FAIL: fix leaves {fix_blue} sky px (> {blue_ok_max}); aperture not closed"); fail = 1

# 2. Sim purity: the draw-only extension must not perturb one sim-consumed byte across
#    the whole intro. Compare the sim fields frame-for-frame (fix vs opt-out).
n = min(len(fix_all), len(opt_all))
sim_diffs = 0
first_diff = None
for k in range(n):
    if sim_fields(fix_all[k]) != sim_fields(opt_all[k]):
        sim_diffs += 1
        if first_diff is None:
            first_diff = (fix_all[k].get("f"), sim_fields(fix_all[k]), sim_fields(opt_all[k]))
if len(fix_all) != len(opt_all):
    print(f"  FAIL: frame count differs (fix {len(fix_all)} vs optout {len(opt_all)})"); fail = 1
if sim_diffs:
    print(f"  FAIL: sim-consumed fields diverge on {sim_diffs}/{n} frames; first at {first_diff}"); fail = 1
else:
    print(f"  sim purity: {n} frames, sim-consumed fields byte-identical fix vs opt-out")

# 3. Fail-on-revert: the opt-out (T13b single-hop) reproduces the aperture leak.
if leak_room in opt_draw:
    print(f"  FAIL: opt-out unexpectedly admits room {leak_room}; the leak is not reproduced (control invalid)"); fail = 1
if opt_blue < blue_leak_min:
    print(f"  FAIL: opt-out shows only {opt_blue} sky px (< {blue_leak_min}); the leak control is invalid"); fail = 1

if fail:
    raise SystemExit(1)
print("PASS: silo_intro_aperture_regression -- aperture closes (draw-only), sim byte-identical, leak reproduced on revert.")
PY

echo "PASS: silo_intro_aperture_regression"
