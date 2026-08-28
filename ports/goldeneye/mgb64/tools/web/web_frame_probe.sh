#!/usr/bin/env bash
# web_frame_probe.sh — browser FRAME-COMPLETENESS regression lane (W4.1).
#
# Goes beyond web_boot_smoke (which only proves the backend presents SOMETHING
# non-black): it reaches REAL GAMEPLAY in a level and asserts no single colour
# floods the viewport — the exact defect class that shipped as the PERF-005
# "sky-flood" bleed, which a non-black check cannot see. It is the net PERF-031
# (Asyncify narrowing) needs before that work touches the async boundary.
#
# Two passes, BOTH must pass for exit 0:
#   1. DETERMINISTIC dam  (?arg=--level&arg=dam&arg=--deterministic)
#      Reproducible boot into gameplay. --deterministic disables the PERF-005
#      async path, so there are no input holds here — this pass proves a clean,
#      repeatable level frame is complete.
#   2. LIVE + THROTTLED dam  (?arg=--level&arg=dam, --throttle 4, held movement)
#      The pass that would have CAUGHT the bleed: CPU-throttled (the flood only
#      reproduced under load) with a held-forward move, bursting screenshots
#      through the movement window. PERF-005b HOLDS incomplete frames (the canvas
#      keeps the last COMPLETE image), so this must see the gate screen or
#      complete gameplay — NEVER a flood. That invariant is the regression
#      contract this lane enforces.
#
# Mirrors web_boot_smoke.sh: SKIP (exit 125, ctest SKIP_RETURN_CODE) when a
# runtime prerequisite is absent (dist/web, a 12 MB GoldenEye ROM, Chrome, node),
# so CI without those skips rather than fails. PASS = exit 0, FAIL = exit 1.
#
# Env overrides (same names as web_boot_smoke.sh):
#   MGB64_DIST     path to a prebuilt dist/web (default <root>/dist/web)
#   MGB64_CHROME   path to a Chrome/Chromium binary
#   MGB64_ROM      path to the GoldenEye ROM (12 MB .z64/.v64/.n64)
#
# Run directly:            tools/web/web_frame_probe.sh
# Run via ctest (labeled): ctest -R web_frame_probe -V     (or: ctest -L web)
set -euo pipefail

SKIP=125
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DIST="${MGB64_DIST:-$ROOT/dist/web}"

skip() { echo "SKIP: $*" >&2; exit "$SKIP"; }

# --- node (the harness runtime) --------------------------------------------
command -v node >/dev/null 2>&1 || skip "node not found (harness runtime)"

# --- prebuilt dist/web (require it prebuilt; do not build here) -------------
for f in index.html mgb64-shell.js ge007_web.js ge007_web.wasm style.css; do
    [ -f "$DIST/$f" ] || skip "dist/web/$f absent — build with tools/web/build_web.sh"
done

# --- a real GoldenEye ROM (12 MB) ------------------------------------------
ROM="${MGB64_ROM:-}"
if [ -z "$ROM" ]; then
    for cand in "$ROOT/baserom.u.z64" "$ROOT/GoldenEye 007 (USA).z64" "$ROOT/baserom.z64"; do
        if [ -f "$cand" ]; then ROM="$cand"; break; fi
    done
fi
[ -n "$ROM" ] && [ -f "$ROM" ] || skip "no readable GoldenEye ROM (looked for repo-root baserom.u.z64 / 'GoldenEye 007 (USA).z64'; \$MGB64_ROM='${MGB64_ROM:-}')"
# Resolve symlinks to a real path (CDP setFileInputFiles needs a concrete file).
ROM="$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"
if command -v python3 >/dev/null 2>&1; then
    ROM="$(python3 -c 'import os,sys;print(os.path.realpath(sys.argv[1]))' "$ROM")"
    sz="$(python3 -c 'import os,sys;print(os.path.getsize(sys.argv[1]))' "$ROM" 2>/dev/null || echo 0)"
    [ "$sz" = "12582912" ] || skip "ROM $ROM is $sz bytes, not 12 MB (12582912) — not a GoldenEye N64 dump"
fi

# --- a Chrome/Chromium binary ----------------------------------------------
CHROME="${MGB64_CHROME:-}"
if [ -z "$CHROME" ]; then
    for cand in \
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
        "/Applications/Chromium.app/Contents/MacOS/Chromium" \
        "$(command -v google-chrome 2>/dev/null || true)" \
        "$(command -v google-chrome-stable 2>/dev/null || true)" \
        "$(command -v chromium 2>/dev/null || true)" \
        "$(command -v chromium-browser 2>/dev/null || true)" \
        "$(command -v chrome 2>/dev/null || true)"; do
        if [ -n "$cand" ] && [ -x "$cand" ]; then CHROME="$cand"; break; fi
    done
fi
[ -n "$CHROME" ] && [ -x "$CHROME" ] || skip "no Chrome/Chromium found — set \$MGB64_CHROME"

# Screenshots land in a throwaway dir OUTSIDE the repo (ROM-derived frames never
# persist in-tree — mirrors the contamination discipline in web_boot_smoke.mjs,
# which decodes in memory and writes nothing). Always cleaned up.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mgb64-frameprobe.XXXXXX")"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

echo "web_frame_probe: dist=$DIST"
echo "web_frame_probe: rom=$ROM"
echo "web_frame_probe: chrome=$CHROME"

DRIVER="$ROOT/tools/web/webcap.mjs"

echo
echo "=== PASS 1/2: deterministic dam (complete-frame assertion) ==="
node "$DRIVER" \
    --dist "$DIST" --rom "$ROM" --chrome "$CHROME" \
    --query "arg=--level&arg=dam&arg=--deterministic" \
    --settle-ms 6000 --assert-complete 90 \
    --out "$WORK/pass1_det.png"

echo
echo "=== PASS 2/2: live + CPU-throttled dam, held movement (the bleed-catcher) ==="
# Non-deterministic (live async), throttle x4 (PERF-005 only reproduced under
# load), a held-forward move that stays down through the burst so the flood
# detector samples the movement-under-load window, then a final resting frame.
node "$DRIVER" \
    --dist "$DIST" --rom "$ROM" --chrome "$CHROME" \
    --query "arg=--level&arg=dam" \
    --throttle 4 --settle-ms 2500 \
    --keys "down:KeyW" --burst "8:350" \
    --assert-complete 90 --budget 90 \
    --out "$WORK/pass2_live.png"

echo
echo "web_frame_probe: BOTH passes complete — frames verified complete."
