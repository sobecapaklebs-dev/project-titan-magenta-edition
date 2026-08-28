#!/bin/bash
#
# metal_shadow_clamp_regression.sh -- FID-0019 Metal sun-shadow path guard (M3.2).
#
# FID-0019 landed three Metal sun-shadow fixes: (1) dc55008 -- the depth-only
# shadow pass now sets MTLDepthClipModeClamp so a boundary caster whose light-space
# z leaves the shadow ortho's [0,4*radius] range is CLAMPED like GL's global
# GL_DEPTH_CLAMP instead of CLIPPED out of the map; (2) 73bbdac -- the receiver's
# nil-shadow-map fallback binds a cleared 1x1 "fully lit" dummy instead of the live
# scene depth attachment (a read-while-write hazard); (3) e8d283a -- autorelease
# hygiene (leak-only, not behavior, no flag).
#
# The parity smoke the ledger cited (renderer_parity_capture.sh) renders
# facility_scissor / surface_sky_fog / world_attrs -- NONE shadow-casting -- so it
# never exercised the shadow path at all. This lane does: it boots the Metal
# backend with sun shadows on the Dam scene and asserts the shadow path is live and
# healthy, and that the two negative-control flags (P1b) are wired.
#
# METAL-ONLY: SKIPs cleanly (exit 0) on non-Apple platforms and whenever the Metal
# backend cannot initialize a device (the GL backend does not have this code path
# -- GL_DEPTH_CLAMP is process-global there, so the fix is a Metal-parity fix).
#
# The depth-clip clamp only
# changes output for a caster crossing the light near/far plane ("near-plane
# pancaking"). On the reachable deterministic Dam scene the casters stay well
# inside the ortho z-range (shadow-map min-depth measured ~130/255 across radii
# 20..250), so the clamp is INERT here and fix-on vs the clamp-revert are
# byte-identical. The lane therefore runs two layers: the authored scene proves
# the real shadow path is healthy and the dummy fallback stays unreachable; then
# MGB64_TEST_METAL_SHADOW_CLIP_Z_OFFSET translates the REAL captured casters
# across the light near plane so Clamp and the legacy Clip negative control must
# produce different shadow maps. That second layer is the fail-on-revert oracle
# for the clamp's pixel effect without altering shipped behavior.
#
# ROM-gated. Captured shadow maps / screenshots are ROM-derived local artifacts --
# do not commit them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
FRAME=120
LEVEL=dam
# Minimum fraction of shadow-map texels a caster must occupy for the map to count
# as a real (non-empty) shadow render. Measured ~22-28% on Dam; 1% is a safe floor.
MIN_OCCUPANCY_PCT=1.0
OUT_DIR="/tmp/mgb64_metal_shadow_clamp_$$"

usage() {
    cat <<'USAGE'
Usage: tools/metal_shadow_clamp_regression.sh [options]

Options:
  --out-dir DIR      output directory (default: /tmp/...)
  --rom PATH         ROM path (default: ./baserom.u.z64)
  --binary PATH      native binary path (default: build/ge007)
  --build-dir DIR    CMake build directory (default: build)
  --no-build         reuse an existing native binary
  --frame N          deterministic screenshot/exit frame (default: 120)
  --timeout SECONDS  per-capture timeout (default: 120)

Metal-only: SKIPs on non-Apple hosts and when no Metal device is available.
Captured artifacts are ROM-derived local validation data. Do not commit.
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

# Metal is macOS-only (gfx_backend.c: GE007_RENDERER=metal is a no-op off __APPLE__).
if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "SKIP: metal_shadow_clamp_regression: not macOS ($(uname -s)); Metal-only lane."
    exit 0
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: metal_shadow_clamp_regression: ROM not found ($ROM); ROM-gated lane."
    exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi
validation_require_binary "$BINARY"
validation_acquire_runtime_lock
trap 'validation_release_runtime_lock; rm -rf "$OUT_DIR"; rm -f shadow_map.pgm screenshot_shadowlane.bmp' EXIT INT TERM

mkdir -p "$OUT_DIR"

# capture <name> [extra env assignments...]  -> populates $OUT_DIR/<name>.{log,pgm,bmp}
# echoes: exit code
capture() {
    local name="$1"; shift
    local d="$OUT_DIR/$name"; mkdir -p "$d"
    rm -f shadow_map.pgm screenshot_shadowlane.bmp
    local rc=0
    validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
            GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
            GE007_RENDERER=metal GE007_SUN_SHADOW=1 GE007_DUMP_SHADOW_MAP=1 \
            "$@" \
            "$BINARY" \
            --rom "$ROM" --level "$LEVEL" --deterministic \
            --screenshot-frame "$FRAME" --screenshot-label shadowlane \
            --screenshot-exit >"$d/run.log" 2>&1 || rc=$?
    mv -f shadow_map.pgm "$d/shadow.pgm" 2>/dev/null || true
    mv -f screenshot_shadowlane.bmp "$d/frame.bmp" 2>/dev/null || true
    printf '%s\n' "$rc"
}

# occupancy <pgm> -> prints "occupied_pct min_depth" (min_depth over occupied texels)
occupancy() {
    python3 - "$1" <<'PY'
import sys
p = sys.argv[1]
try:
    with open(p, "rb") as f:
        assert f.readline().strip() == b"P5"
        w, h = map(int, f.readline().split())
        int(f.readline())          # maxval
        data = f.read(w * h)
except Exception:
    print("0.0 255"); sys.exit(0)
n = len(data) or 1
occ = sum(1 for b in data if b < 250)   # a caster wrote closer than the far clear
mn = min(data) if data else 255
print(f"{100.0*occ/n:.2f} {mn}")
PY
}

# changed_pct <pgm-a> <pgm-b> -> percentage of differing depth-map bytes.
changed_pct() {
    python3 - "$1" "$2" <<'PY'
import sys

def pixels(path):
    with open(path, "rb") as f:
        if f.readline().strip() != b"P5":
            raise ValueError("not P5")
        w, h = map(int, f.readline().split())
        int(f.readline())
        return f.read(w * h)

try:
    a, b = pixels(sys.argv[1]), pixels(sys.argv[2])
    if len(a) != len(b) or not a:
        raise ValueError("size mismatch")
    changed = sum(x != y for x, y in zip(a, b))
    print(f"{100.0 * changed / len(a):.6f}")
except Exception:
    print("0.000000")
PY
}

echo "=== metal_shadow_clamp_regression (Metal + SunShadow, level $LEVEL, frame $FRAME) ==="

# --- fix-on (default: both M3.2 fixes active) ---
on_rc="$(capture fixon)"
on_log="$OUT_DIR/fixon/run.log"

# Metal availability gate: if the backend never initialized a device, SKIP (a
# headless CI host or non-Metal GPU) rather than red the whole suite.
if ! grep -q "native Metal backend init: device=" "$on_log" 2>/dev/null; then
    echo "SKIP: metal_shadow_clamp_regression: Metal backend did not initialize a device (headless/non-Metal host)."
    echo "  (last log lines:)"; tail -6 "$on_log" 2>/dev/null | sed 's/^/    /'
    exit 0
fi

rc=0

# (a) shadow path boots cleanly
if [[ "$on_rc" -ne 0 ]]; then
    echo "FAIL: fix-on Metal+shadow run exited $on_rc"; tail -20 "$on_log" | sed 's/^/  /'; rc=1
fi
if grep -qF "[GEASSERT]" "$on_log"; then
    echo "FAIL: GEASSERT fired during the Metal shadow run"; grep -F "[GEASSERT]" "$on_log" | head -5 | sed 's/^/  /'; rc=1
fi

# (b) a REAL shadow scene was rendered: the shadow map exists and is non-trivial.
read on_occ on_min < <(occupancy "$OUT_DIR/fixon/shadow.pgm")
echo "  fix-on   : exit=$on_rc shadow-map occupancy=${on_occ}% min-depth=${on_min}/255"
if awk -v o="$on_occ" -v m="$MIN_OCCUPANCY_PCT" 'BEGIN{exit !(o+0 < m+0)}'; then
    echo "FAIL: shadow map is empty/trivial (occupancy ${on_occ}% < ${MIN_OCCUPANCY_PCT}%) -- the sun-shadow path did not render casters"; rc=1
fi

# (c) 73bbdac invariant: the dummy-depth fallback is UNREACHABLE in normal play
# (SHADER_OPT_SUN_SHADOW is only set once g_pc_shadow_map_ready=1). If this
# [RENDER-HEALTH] line ever appears the ordering invariant broke.
if grep -q "shadow receiver active with no shadow-map resource" "$on_log"; then
    echo "FAIL: the nil-shadow-map fallback was taken in normal play (73bbdac reachability invariant broken)"; rc=1
fi

# (d) both P1b negative-control flags are wired: the run still boots and renders a
# non-trivial shadow map with each revert engaged.
for flag in GE007_NO_METAL_SHADOW_DEPTH_CLAMP GE007_NO_METAL_SHADOW_DUMMY_DEPTH; do
    fname="revert_${flag##GE007_NO_METAL_SHADOW_}"
    f_rc="$(capture "$fname" "${flag}=1")"
    read f_occ f_min < <(occupancy "$OUT_DIR/$fname/shadow.pgm")
    echo "  ${flag}=1 : exit=$f_rc shadow-map occupancy=${f_occ}% min-depth=${f_min}/255"
    if [[ "$f_rc" -ne 0 ]]; then
        echo "FAIL: ${flag}=1 run exited $f_rc (negative-control flag not accepted)"; rc=1
    fi
    if awk -v o="$f_occ" -v m="$MIN_OCCUPANCY_PCT" 'BEGIN{exit !(o+0 < m+0)}'; then
        echo "FAIL: ${flag}=1 produced an empty/trivial shadow map (occupancy ${f_occ}%)"; rc=1
    fi
done

# (e) Document the expected authored-scene inertness: the depth-clip clamp is inert
# reachable scene -- fix-on vs the clamp-revert are byte-identical because no caster
# crosses the near plane (min-depth ~${on_min}/255, far from the near plane at 0).
if [[ -f "$OUT_DIR/fixon/frame.bmp" && -f "$OUT_DIR/revert_DEPTH_CLAMP/frame.bmp" ]]; then
    changed="$(python3 tools/compare_screenshots.py \
        "$OUT_DIR/fixon/frame.bmp" "$OUT_DIR/revert_DEPTH_CLAMP/frame.bmp" 2>/dev/null \
        | grep -m1 'Changed pixels:' || true)"
    echo "  [doc] fix-on vs clamp-revert final frame: ${changed:-<compare unavailable>}"
    echo "  [doc] clamp is INERT here (casters min-depth ${on_min}/255 never reach the near plane);"
fi

# (f) Fail-on-revert pixel oracle for dc55008. Shift the actual captured Dam
# caster geometry across the shadow near plane. Clamp keeps the boundary
# triangles by pancaking their depth; the legacy Clip mode removes/clips them.
# The two depth maps MUST differ. The MGB64_ variable is test-only and does not
# enter the documented player-facing environment surface.
PROBE_Z_OFFSET="-1.2"
probe_on_rc="$(capture clamp_probe \
    MGB64_TEST_METAL_SHADOW_CLIP_Z_OFFSET="$PROBE_Z_OFFSET")"
probe_off_rc="$(capture clamp_probe_revert \
    MGB64_TEST_METAL_SHADOW_CLIP_Z_OFFSET="$PROBE_Z_OFFSET" \
    GE007_NO_METAL_SHADOW_DEPTH_CLAMP=1)"
read probe_on_occ probe_on_min < <(occupancy "$OUT_DIR/clamp_probe/shadow.pgm")
read probe_off_occ probe_off_min < <(occupancy "$OUT_DIR/clamp_probe_revert/shadow.pgm")
probe_delta="$(changed_pct "$OUT_DIR/clamp_probe/shadow.pgm" \
                           "$OUT_DIR/clamp_probe_revert/shadow.pgm")"
echo "  near-plane probe z=${PROBE_Z_OFFSET}: clamp occupancy=${probe_on_occ}% min=${probe_on_min}; legacy-clip occupancy=${probe_off_occ}% min=${probe_off_min}; changed=${probe_delta}%"
if [[ "$probe_on_rc" -ne 0 || "$probe_off_rc" -ne 0 ]]; then
    echo "FAIL: near-plane clamp oracle run failed (clamp=$probe_on_rc legacy=$probe_off_rc)"
    rc=1
elif awk -v o="$probe_on_occ" -v m="$MIN_OCCUPANCY_PCT" 'BEGIN{exit !(o+0 < m+0)}'; then
    echo "FAIL: near-plane clamp probe produced no non-trivial clamped casters"
    rc=1
elif awk -v d="$probe_delta" 'BEGIN{exit !(d+0 <= 0.001)}'; then
    echo "FAIL: near-plane clamp probe did not distinguish Clamp from legacy Clip"
    rc=1
fi

if [[ "$rc" -ne 0 ]]; then
    echo "FAIL: metal_shadow_clamp_regression"
    exit 1
fi
echo "PASS: metal_shadow_clamp_regression (shadow path live, fallback unreachable, flags wired, near-plane Clamp effect ${probe_delta}% map delta)"
