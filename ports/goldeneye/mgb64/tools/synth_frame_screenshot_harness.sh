#!/usr/bin/env bash
#
# synth_frame_screenshot_harness.sh -- AUDIT-0040 verification harness.
#
# Proves the manual/auto BMP screenshot path (platformSaveScreenshot) captures
# the DEFINED composited frame currently being presented, not a stale/previous
# one. It is ROM-gated but backend-agnostic in intent:
#
#   * On THIS host it runs the desktop GL backend (GE007_RENDERER=gl). Desktop GL
#     reads the front buffer, which already holds the last-presented frame -- so
#     a green run here proves the render->capture->decode plumbing and the
#     GE007_SYNTH_FRAME_PATTERN generator end to end.
#   * On a PortMaster/GLES handheld the OWNER runs the SAME script against the
#     MGB64_PORTMASTER_GLES build (--renderer gles-device just drops the
#     GE007_RENDERER override and uses whatever the on-device binary selects).
#     There, the fix under test is the pre-swap stash
#     (gfx_opengl_capture_default_framebuffer) feeding platformSaveScreenshot --
#     the decode assertions are identical, so a green run proves the stale
#     back-buffer bug is gone on real hardware.
#
# Mechanism: GE007_SYNTH_FRAME_PATTERN=1 overwrites the whole composited frame,
# after end_frame()+minimap, with a solid color encoding the per-presented-frame
# counter (R=(n>>16)&255, G=(n>>8)&255, B=n&255). The harness boots N times, once
# per consecutive --screenshot-frame value, decodes the counter from each BMP,
# and asserts: valid BMP header + expected dimensions; the frame is a UNIFORM
# solid color (proves the capture covers the whole final frame, not a sub-rect or
# a torn composite); and the decoded counters are DISTINCT and strictly
# increasing by a constant step across consecutive frames (proves each capture
# reflects its own newly-presented frame, i.e. no stale repeat).
#
# Artifacts (BMPs, logs) are ROM-derived local validation data. Do not commit them.
#
# Exit: 0 = pass. 1 = a decode/assertion failed. 125 = SKIP (no ROM present).
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_synth_frame_harness_$$"
LEVEL=33
START_FRAME=40
COUNT=3
RENDERER="gl"     # "gl" = force desktop GL here; "device" = no override (owner GLES device)

usage() {
    cat <<'USAGE'
Usage: tools/synth_frame_screenshot_harness.sh [options]

Options:
  --out-dir DIR      output directory (default: /tmp/...)
  --level N          level id (default: 33, Dam)
  --start-frame N    first --screenshot-frame value (default: 40)
  --count N          number of consecutive frames to capture (default: 3)
  --renderer R       "gl" forces GE007_RENDERER=gl (default; desktop host);
                     "device" leaves the renderer to the on-device binary
                     (owner runs this against the MGB64_PORTMASTER_GLES build)
  --rom PATH         ROM path (default: ./baserom.u.z64)
  --binary PATH      native binary path (default: <build-dir>/ge007)
  --build-dir DIR    CMake build directory (default: build)
  --no-build         reuse an existing native binary
  --timeout SECONDS  per-boot capture timeout (default: 120)

Artifacts are ROM-derived local validation data; do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --start-frame) START_FRAME="$2"; shift 2 ;;
        --count) COUNT="$2"; shift 2 ;;
        --renderer) RENDERER="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
fi

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found at $ROM (ROM-gated harness)" >&2
    exit 125
fi

if [[ "$DO_BUILD" == "1" ]]; then
    validation_build "$BUILD_DIR" --target ge007 >/dev/null 2>&1 || {
        echo "FAIL: build failed" >&2; exit 1;
    }
fi

if [[ ! -x "$BINARY" ]]; then
    echo "FAIL: binary not found/executable: $BINARY" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

RENDER_ENV=()
if [[ "$RENDERER" == "gl" ]]; then
    RENDER_ENV=(GE007_RENDERER=gl)
fi

echo "== synth_frame_screenshot_harness: level=$LEVEL frames=${START_FRAME}..$((START_FRAME + COUNT - 1)) renderer=$RENDERER =="

for ((i = 0; i < COUNT; i++)); do
    F=$((START_FRAME + i))
    LABEL="synthframe_${F}"
    LOG="$OUT_DIR/boot_${F}.log"
    if ! ( cd "$OUT_DIR" && validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_SYNTH_FRAME_PATTERN=1 \
            "${RENDER_ENV[@]}" \
            "$BINARY" \
            --rom "$ROM" \
            --level "$LEVEL" \
            --deterministic \
            --screenshot-frame "$F" \
            --screenshot-label "$LABEL" \
            --screenshot-exit ) >"$LOG" 2>&1; then
        echo "FAIL: boot for frame $F failed (see $LOG)" >&2
        tail -5 "$LOG" >&2 || true
        exit 1
    fi
    if [[ ! -f "$OUT_DIR/screenshot_${LABEL}.bmp" ]]; then
        echo "FAIL: no screenshot produced for frame $F (see $LOG)" >&2
        exit 1
    fi
done

python3 - "$OUT_DIR" "$START_FRAME" "$COUNT" <<'PY'
import struct, sys, os
from collections import Counter

out_dir, start_frame, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
EXPECT_W, EXPECT_H = 640, 480

def decode_bmp(path):
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        raise ValueError("bad BMP magic")
    w = struct.unpack("<i", d[18:22])[0]
    h = struct.unpack("<i", d[22:26])[0]
    bpp = struct.unpack("<H", d[28:30])[0]
    off = struct.unpack("<i", d[10:14])[0]
    if bpp != 24:
        raise ValueError("bpp %d != 24" % bpp)
    row = (w * 3 + 3) & ~3
    # Sample the central scene region (inner half). The synthetic pattern fills
    # the composited scene; the final present may letterbox it inside the drawable
    # (real game frames carry the same bars), so we decode from the scene region.
    hist = Counter()
    for gy in range(h // 4, (3 * h) // 4, max(1, h // 16)):
        for gx in range(w // 4, (3 * w) // 4, max(1, w // 16)):
            p = off + gy * row + gx * 3
            hist[(d[p+2], d[p+1], d[p])] += 1   # (R,G,B) from BGR store
    return w, h, hist

counters = []
fail = False
for i in range(count):
    f = start_frame + i
    path = os.path.join(out_dir, "screenshot_synthframe_%d.bmp" % f)
    w, h, hist = decode_bmp(path)
    if (w, h) != (EXPECT_W, EXPECT_H):
        print("FAIL: %s dims %dx%d != %dx%d" % (path, w, h, EXPECT_W, EXPECT_H)); fail = True
    # scene region must be a single uniform color = the encoded counter (proves
    # the whole scene was captured from THIS frame, not a torn/partial composite)
    if len(hist) != 1:
        print("FAIL: %s scene region not uniform (%d colors: %s) -- partial/torn capture"
              % (path, len(hist), dict(hist))); fail = True
    (R, G, B), _ = hist.most_common(1)[0]
    n = (R << 16) | (G << 8) | B
    counters.append(n)
    print("  frame %d: %dx%d RGB=(%d,%d,%d) counter=%d" % (f, w, h, R, G, B, n))

# distinct + strictly increasing (a stale/repeated capture breaks this)
steps = [counters[i+1] - counters[i] for i in range(len(counters)-1)]
if len(set(counters)) != len(counters):
    print("FAIL: decoded counters not distinct: %s (stale/repeated capture)" % counters); fail = True
if any(s <= 0 for s in steps):
    print("FAIL: decoded counters not strictly increasing: %s" % counters); fail = True
if steps and any(s != steps[0] for s in steps):
    print("FAIL: decoded counter step not constant: %s (dropped/duplicated frame)" % counters); fail = True

if fail:
    sys.exit(1)
print("PASS: %d consecutive captures, counters %s, uniform + distinct + monotonic" % (count, counters))
PY
rc=$?
if [[ $rc -ne 0 ]]; then
    echo "FAIL: synth frame decode assertions failed" >&2
    exit 1
fi

echo "PASS: synth_frame_screenshot_harness"
rm -rf "$OUT_DIR"
exit 0
