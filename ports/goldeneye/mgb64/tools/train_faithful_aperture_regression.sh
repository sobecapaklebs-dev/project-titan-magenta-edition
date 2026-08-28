#!/bin/bash
#
# train_faithful_aperture_regression.sh -- Guard FID-0008: Train room-51 window
# grazing-aperture fill under --faithful.
#
# Under --faithful the port's visibility wideners are OFF (they widen room_rendered,
# which perturbs the sim RNG via PROPFLAG_ONSCREEN, so --faithful disables them for
# sim fidelity). The Train room-51 rear-office window (the Trevelyan "that's close
# enough" beat) frames room 53 through venetian slats whose projected portal aperture
# collapses to an empty 2D bbox, so the pure portal BFS drops room 53 and the window
# shows a wrong (missing-geometry) fill -- retail's BFS reaches room 53
# (docs/RENDERING_REGRESSION_NOTES.md).
#
# The fix (bg.c, FID-0008) runs the visibility-supplement as a DRAW-ONLY pass under
# --faithful: room 53 enters the DRAW list (window covered) but never room_rendered,
# so the faithful sim is byte-identical. This lane proves, at the pad-74 pose:
#   1. default (fix on): room 53 is admitted DRAW-ONLY (in the draw list, NOT
#      sim-rendered) and the window fill matches the wideners-on look.
#   2. sim purity: every sim-consumed field is BYTE-IDENTICAL, frame-for-frame, with
#      the fix on vs the GE007_NO_FAITHFUL_DRAW_ONLY_WIDENERS opt-out -- the
#      room_rendered read-back set (FID-0012), the rendered-room count, the
#      room_neighbor count, the RNG call count, and the onscreen actor count.
#   3. fail-on-revert: GE007_NO_FAITHFUL_DRAW_ONLY_WIDENERS=1 (pre-fix --faithful)
#      drops room 53 from the draw list and the window fill reverts -- reddening
#      this lane.
#
# ROM-gated: SKIPs cleanly without the ROM or Pillow. Artifacts are ROM-derived
# local validation data -- do not commit captured screenshots, traces, or logs.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_train_faithful_aperture_$$"
FRAME=120
LEVEL=train
LEAK_ROOM=53              # grazing far-room dropped by the pure BFS under --faithful
WINDOW_ROI="265 150 565 270"   # the rear-office window bbox (x0 y0 x1 y1); 300x120 = 36000 px

# Corroborator floor for check 4 (the raw-pixel window-ROI gate), expressed as a
# FRACTION of the ROI area (300x120 = 36000 px) rather than a hand-tuned absolute so
# it tracks the ROI and is renderer-independent. Check 4 is a CORROBORATOR only --
# checks 1-3 (draw-only admission, sim byte-identity, fail-on-revert) are the
# load-bearing gate. This is a GROSS-REGRESSION floor: it trips only when the fix has
# essentially no visual effect on the window (the draw-only widener stopped covering
# room 53), not when the exact pixel count drifts with the renderer.
#
# Why so small: at the pad-74 pose the rear-office window is a venetian slat panel
# that occludes almost all of room 53, so admitting room 53 draw-only changes only a
# thin aperture -- the visible signal is intrinsically tiny. The old 300 px absolute
# was tuned against a different (pre-WebGPU) renderer/capture and never matched the
# current default backend.
#
# Calibration (2026-07-15, macOS, binary build/ge007, pinned env below):
#   working fix vs opt-out ROI delta = 28 px (0.078% of ROI), rock-stable across 3
#     runs (28/28/28); run-to-run captures byte-identical (fix_i vs fix_j = 0 px).
#   regressed-fix proxy (opt-out vs opt-out) = 0 px across 3 runs -- a fix that stops
#     admitting room 53 renders identically to the opt-out control.
#   floor = 0.02% * 36000 = 7 px: clears the 28 px working signal with ~4x headroom,
#     while a 0 px (no-effect) regression fails by a 7 px margin.
WINDOW_MIN_DELTA_FRACTION=0.0002   # 0.02% of ROI area -> 7 px floor at the 300x120 ROI

usage() {
    cat <<'USAGE'
Usage: tools/train_faithful_aperture_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frame N            screenshot/exit frame (default: 120)
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
    echo "SKIP: train_faithful_aperture_regression: Pillow not installed (pip install pillow)."
    exit 0
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: train_faithful_aperture_regression: ROM not found ($ROM); ROM-gated lane."
    exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

# Capture-environment pins (make check 4 deterministic across hosts):
#   GE007_RENDERER=webgpu     -- pin the backend to the current default so a host
#       with GE007_RENDERER=gl/metal in its environment cannot perturb the capture.
#   GE007_WINDOW_SIZE=1440x810 -- pin the drawable to a canonical 16:9 so the fixed
#       640x480 screenshot resamples identically everywhere (headless windows here
#       are backing-scale 1: a 640x480 window yields a 640x480 drawable). This 16:9
#       framing is what preserves the room-53 aperture signal -- a 4:3 pin narrows the
#       FOV and the thin apertures vanish (delta -> 0). NOTE: we deliberately do NOT
#       use GE007_DIAG_SCREENSHOT_NATIVE_SIZE: it makes the screenshot the raw
#       host-dependent drawable (1440x810 here), which breaks the 640x480 WINDOW_ROI.
# capture <name> [extra env assignments...]
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
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_AUTO_WARP_FRAME=40 GE007_AUTO_WARP_PAD=74 \
            GE007_TRACE_FULL_ROOM_LIST=1 \
            GE007_RENDERER=webgpu GE007_WINDOW_SIZE=1440x810 \
            "$@" \
            "$BINARY" \
            --savedir "$d/save" --rom "$ROM" --level "$LEVEL" --deterministic --faithful \
            --trace-state "$d/trace.jsonl" \
            --screenshot-frame "$FRAME" --screenshot-label "$name" \
            --screenshot-exit
    ) >"$d/run.log" 2>&1; then
        echo "FAIL: capture $name failed" >&2
        tail -30 "$d/run.log" | sed 's/^/  /' >&2
        exit 1
    fi
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

echo "=== Train Faithful Aperture Regression (FID-0008, level $LEVEL, pad 74, frame $FRAME) ==="
echo "  binary: $BINARY"
echo "  rom:    $ROM"

capture fix
capture optout GE007_NO_FAITHFUL_DRAW_ONLY_WIDENERS=1

python3 - "$OUT_DIR" "$LEAK_ROOM" "$WINDOW_MIN_DELTA_FRACTION" $WINDOW_ROI <<'PY'
import json, sys
from pathlib import Path
from PIL import Image, ImageChops

root = Path(sys.argv[1]); leak_room = int(sys.argv[2])
min_delta_fraction = float(sys.argv[3])
roi = tuple(int(x) for x in sys.argv[4:8])
roi_area = (roi[2] - roi[0]) * (roi[3] - roi[1])
min_delta = max(1, int(roi_area * min_delta_fraction))   # gross-regression floor

def all_traces(name):
    recs = []
    for line in (root / name / "trace.jsonl").open():
        line = line.strip()
        if line:
            recs.append(json.loads(line))
    if not recs:
        raise SystemExit(f"FAIL: no trace records for {name}")
    return recs

def draw_set(rec):
    return set(rec["rooms"]["vis"].get("draw_sample") or [])

def rendered_set(rec):
    return set(rec["rooms"]["vis"].get("sample") or [])

def sim_fields(rec):
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
fix_draw, fix_rendered = draw_set(fix), rendered_set(fix)
opt_draw = draw_set(optout)

fail = 0
print(f"  fix    : draw={sorted(fix_draw)} rendered={sorted(fix_rendered)}")
print(f"  optout : draw={sorted(opt_draw)} rendered={sorted(rendered_set(optout))}")

# 1. The fix admits the grazing room DRAW-ONLY.
if leak_room not in fix_draw:
    print(f"  FAIL: fix must admit grazing room {leak_room} to the draw list"); fail = 1
if leak_room in fix_rendered:
    print(f"  FAIL: grazing room {leak_room} must be DRAW-ONLY (not sim-rendered) under the fix"); fail = 1

# 2. Sim purity: sim-consumed fields byte-identical frame-for-frame.
n = min(len(fix_all), len(opt_all))
sim_diffs = 0; first_diff = None
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

# 3. Fail-on-revert: the opt-out drops the grazing room.
if leak_room in opt_draw:
    print(f"  FAIL: opt-out unexpectedly admits room {leak_room}; the leak is not reproduced (control invalid)"); fail = 1

# 4. CORROBORATOR (not load-bearing): the window fill visibly changes fix vs opt-out.
#    Gross-regression floor only -- checks 1-3 are the authoritative sim gate. The
#    signal is a thin venetian-slat aperture, so this just confirms the draw-only
#    widener still puts *something* of room 53 on screen; it does not pin exact px.
ia = Image.open(root / "fix" / "f.png").convert("RGB").crop(roi)
ib = Image.open(root / "optout" / "f.png").convert("RGB").crop(roi)
diff = ImageChops.difference(ia, ib)
changed = sum(1 for p in diff.getdata() if max(p) > 16)
print(f"  window ROI changed px (fix vs opt-out): {changed} "
      f"(corroborator floor {min_delta}px = {100*min_delta/roi_area:.3f}% of {roi_area}px ROI)")
if changed < min_delta:
    print(f"  FAIL: corroborator -- window fill shows no visual effect from the fix "
          f"({changed}px < {min_delta}px floor); the draw-only widener no longer covers "
          f"room {leak_room} (checks 1-3 are the authoritative sim gate)"); fail = 1

if fail:
    raise SystemExit(1)
print("PASS: train_faithful_aperture_regression -- room 53 drawn draw-only, sim byte-identical, leak reproduced on revert.")
PY

echo "PASS: train_faithful_aperture_regression"
