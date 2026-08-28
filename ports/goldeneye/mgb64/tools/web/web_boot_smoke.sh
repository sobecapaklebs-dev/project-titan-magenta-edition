#!/usr/bin/env bash
# web_boot_smoke.sh — headless-Chrome boot smoke for the MGB64 browser build.
#
# Serves a prebuilt dist/web on an ephemeral localhost port and drives a system
# Chrome (via web_boot_smoke.mjs, CDP-over-pipe, zero npm deps) to boot the demo
# end-to-end with a real ROM and assert the WebGPU engine reaches a live rendering
# state. See web_boot_smoke.mjs for the asserted contract.
#
# SKIP (exit 125, ctest SKIP_RETURN_CODE) when a runtime prerequisite is absent —
# dist/web, a real GoldenEye ROM, Chrome, or node — so CI without those simply
# skips rather than fails. PASS = exit 0, FAIL = exit 1.
#
# Env overrides:
#   MGB64_DIST               path to a prebuilt dist/web (default <root>/dist/web)
#   MGB64_CHROME             path to a Chrome/Chromium binary
#   MGB64_ROM                path to the GoldenEye ROM (12 MB .z64/.v64/.n64)
#   MGB64_WEB_SMOKE_BUDGET   boot budget in seconds (default 45)
# Extra args after `--` are forwarded to the .mjs driver (e.g. --no-screenshot).
#
# Run directly:            tools/web/web_boot_smoke.sh
# Run via ctest (labeled): ctest -R web_boot_smoke -V     (or: ctest -L web)
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
# Repo root carries gitignored symlinks (baserom.u.z64 / 'GoldenEye 007 (USA).z64').
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

echo "web_boot_smoke: dist=$DIST"
echo "web_boot_smoke: rom=$ROM"
echo "web_boot_smoke: chrome=$CHROME"

# Forward anything after `--` to the driver.
extra=()
while [ "$#" -gt 0 ]; do case "$1" in --) shift; extra=("$@"); break;; *) shift;; esac; done

# ${extra[@]+...} guards bash 3.2 (macOS system bash), where expanding an
# empty array under `set -u` aborts with "unbound variable".
exec node "$ROOT/tools/web/web_boot_smoke.mjs" \
    --dist "$DIST" --rom "$ROM" --chrome "$CHROME" ${extra[@]+"${extra[@]}"}
