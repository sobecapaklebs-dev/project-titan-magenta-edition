#!/usr/bin/env bash
# One-command web build. Usage: tools/web/build_web.sh [--debug]
# Produces dist/web/ (ge007_web.js, ge007_web.wasm, index.html, shell js/css).
set -euo pipefail
EMSDK_VERSION="4.0.10"   # floor: first release bundling the emdawnwebgpu port
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"
if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
    echo "emsdk not found at $EMSDK_DIR — install:" >&2
    echo "  git clone https://github.com/emscripten-core/emsdk.git $EMSDK_DIR" >&2
    echo "  $EMSDK_DIR/emsdk install $EMSDK_VERSION && $EMSDK_DIR/emsdk activate $EMSDK_VERSION" >&2
    exit 3
fi
# shellcheck disable=SC1091
source "$EMSDK_DIR/emsdk_env.sh" >/dev/null
# WEB-060: emsdk floor-vs-pin policy. CI pins EMSDK_VERSION exactly (web-demo.yml's
# setup-emsdk `version:`); local builds accept that floor OR ANY NEWER emcc. Warn
# loudly and stop on an older toolchain unless MGB64_EMSDK_ANY=1 overrides (escape
# hatch for a patched/bleeding-edge SDK). Parse-fail = warn-and-continue (cmake
# will error loudly later if the toolchain is truly broken).
emcc_ver="$(emcc --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
if [ -z "$emcc_ver" ]; then
    echo "WARNING: could not parse 'emcc --version' — skipping the $EMSDK_VERSION floor check" >&2
elif [ "$(printf '%s\n%s\n' "$EMSDK_VERSION" "$emcc_ver" | sort -V | head -1)" != "$EMSDK_VERSION" ]; then
    echo "############################################################" >&2
    echo "ERROR: emcc $emcc_ver is OLDER than the required $EMSDK_VERSION floor." >&2
    echo "       The emdawnwebgpu WebGPU port this build needs first shipped in $EMSDK_VERSION." >&2
    if [ "${MGB64_EMSDK_ANY:-}" = "1" ]; then
        echo "       MGB64_EMSDK_ANY=1 set — continuing anyway (unsupported)." >&2
    else
        echo "       Install $EMSDK_VERSION (see docs/WEB.md), or set MGB64_EMSDK_ANY=1 to override." >&2
        echo "############################################################" >&2
        exit 4
    fi
elif [ "$emcc_ver" != "$EMSDK_VERSION" ]; then
    echo "note: emcc $emcc_ver (>= $EMSDK_VERSION floor); CI pins $EMSDK_VERSION." >&2
fi
BUILD_TYPE="Release"; [ "${1:-}" = "--debug" ] && BUILD_TYPE="Debug"
emcmake cmake -S "$ROOT" -B "$ROOT/build-web" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$ROOT/build-web" -j8 --target ge007_web
# WEB-061: clean-stage dist/web so stale local artifacts never survive a rebuild
# (CI starts from a fresh checkout, so this only bites local trees).
rm -rf "$ROOT/dist/web"
mkdir -p "$ROOT/dist/web"
cp "$ROOT/build-web/ge007_web.js" "$ROOT/build-web/ge007_web.wasm" "$ROOT/dist/web/"
# PERF-036 (build half): sw.js joins the static shell so the deployed set is 6
# files (index.html, mgb64-shell.js, style.css, sw.js + the two engine files).
# Keep web-demo.yml's whitelist and docs/WEB.md's invariant in lockstep with this.
cp "$ROOT"/web/index.html "$ROOT"/web/mgb64-shell.js "$ROOT"/web/style.css "$ROOT"/web/sw.js "$ROOT/dist/web/"
# WEB-032 + PERF-036 (build half): stamp the STAGED copies with a short build hash.
# The shell uses it to load ge007_web.js?v=HASH / fetch ge007_web.wasm?v=HASH (so
# cached glue never pairs with fresh wasm across a Pages redeploy); sw.js uses the
# SAME hash to key its cache generation (CACHE_NAME = "mgb64-"+HASH), so each
# deploy is its own atomic cache and the service worker's byte content changes,
# triggering the browser's SW update. The source files keep the __MGB64_BUILD__
# placeholder — rewrite the staged copies only; if the placeholder isn't present
# the sed is a harmless no-op. Portable sed via temp file (not -i: the CI runner is
# Linux, dev machines are macOS/BSD). BUILD_HASH is a git short hash or a unix
# timestamp — both sed-safe (no slashes/metacharacters).
BUILD_HASH="$(git -C "$ROOT" rev-parse --short=8 HEAD 2>/dev/null || date +%s)"
for stamp in mgb64-shell.js sw.js; do
    sed "s/__MGB64_BUILD__/$BUILD_HASH/g" "$ROOT/dist/web/$stamp" > "$ROOT/dist/web/$stamp.tmp" \
        && mv "$ROOT/dist/web/$stamp.tmp" "$ROOT/dist/web/$stamp"
done
echo "dist/web ready (build $BUILD_HASH):"; ls -la "$ROOT/dist/web"
