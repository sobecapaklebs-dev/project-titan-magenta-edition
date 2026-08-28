#!/usr/bin/env bash
#
# Local MinGW cross-compile lane (backlog MW.2): prove the Windows build
# compiles and links from this macOS/Linux host, instead of discovering breaks
# only when the release CI runs on windows-latest.
#
# It mirrors the release CI's Windows job (.github/workflows/release.yml):
#   toolchain  mingw-w64-x86_64-gcc   (brew install mingw-w64)
#   SDL2       mingw-w64-x86_64-SDL2  (vendored from repo.msys2.org, pinned below)
#   config     -DMGB64_APP=ON -DCMAKE_BUILD_TYPE=Release -DPORT_VALIDATION_TESTS=OFF
#   target     ge007  (-> ge007.exe)
#
# The build exercises the WIN32 branches in CMakeLists.txt: -mno-ms-bitfields
# (the v0.3.2 crash-on-load fix), the winpthread link, and the nfd COM libs.
#
# Idempotent and safe to re-run. Warnings are reported but do NOT fail the lane
# (matching the CI's warning posture); only compile/link errors fail it. The
# warning count covers the TUs compiled by THIS invocation — pass --clean for
# a full-tree census.
#
# Usage:  tools/mingw_cross_check.sh [--clean] [--jobs N]
set -euo pipefail

# --- Pinned SDL2 provenance ----------------------------------------------------
# The release CI installs mingw-w64-x86_64-SDL2 via MSYS2 pacman (update: true),
# i.e. the latest at CI-run time. We vendor that exact package from the MSYS2
# repo and pin the version + the SHA256 published in the official mingw64.db.
SDL2_PKG="mingw-w64-x86_64-SDL2-2.32.10-1-any.pkg.tar.zst"
SDL2_URL="https://repo.msys2.org/mingw/mingw64/${SDL2_PKG}"
SDL2_SHA256="5991afbcfeb2f8b838ab80b2270d713a727199ca392715677a7c1931a0d9ecef"

MINGW_TARGET="x86_64-w64-mingw32"
JOBS=8
CLEAN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1 ;;
        --jobs) JOBS="$2"; shift ;;
        --jobs=*) JOBS="${1#*=}" ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

DEPS_DIR="$REPO_ROOT/build-mingw-deps"
PREFIX="$DEPS_DIR/prefix/mingw64"
BUILD_DIR="$REPO_ROOT/build-mingw"
ARTIFACT="$BUILD_DIR/ge007.exe"

fail() { echo ""; echo "MINGW CROSS-CHECK: FAIL — $1"; exit 1; }

sha256_of() {
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}';
    elif command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}';
    else fail "no shasum/sha256sum available"; fi
}

# --- 1. Toolchain --------------------------------------------------------------
CC_BIN="$(command -v ${MINGW_TARGET}-gcc || true)"
[ -n "$CC_BIN" ] || fail "${MINGW_TARGET}-gcc not found on PATH (brew install mingw-w64)"
echo "toolchain: $($CC_BIN --version | head -1)  [$CC_BIN]"

# --- 2. Vendored SDL2 (fetch + verify + extract on first run) ------------------
if [ "$CLEAN" = "1" ]; then rm -rf "$BUILD_DIR" "$DEPS_DIR/prefix"; fi

if [ ! -f "$PREFIX/include/SDL2/SDL.h" ] || [ ! -f "$PREFIX/lib/libSDL2.dll.a" ]; then
    echo "vendoring SDL2 ($SDL2_PKG) ..."
    mkdir -p "$DEPS_DIR/pkgcache"
    PKG_PATH="$DEPS_DIR/pkgcache/$SDL2_PKG"
    if [ ! -f "$PKG_PATH" ]; then
        curl -fsSL -o "$PKG_PATH" "$SDL2_URL" || fail "SDL2 download failed: $SDL2_URL"
    fi
    GOT="$(sha256_of "$PKG_PATH")"
    if [ "$GOT" != "$SDL2_SHA256" ]; then
        rm -f "$PKG_PATH"
        fail "SDL2 checksum mismatch: got $GOT expected $SDL2_SHA256"
    fi
    echo "SDL2 checksum OK ($SDL2_SHA256)"
    command -v zstd >/dev/null 2>&1 || fail "zstd required to extract the .pkg.tar.zst (brew install zstd)"
    rm -rf "$DEPS_DIR/prefix"; mkdir -p "$DEPS_DIR/prefix"
    zstd -dc "$PKG_PATH" | tar -xf - -C "$DEPS_DIR/prefix" 2>/dev/null
    [ -f "$PREFIX/include/SDL2/SDL.h" ] || fail "SDL2 extraction did not yield include/SDL2/SDL.h"
fi
echo "SDL2 prefix: $PREFIX"

# --- 3. Configure --------------------------------------------------------------
GEN="Unix Makefiles"   # ninja not required; generator does not affect codegen
echo "configuring ($GEN, Release, MGB64_APP=ON) ..."
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G "$GEN" \
    -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/cmake/mingw-w64-x86_64.cmake" \
    -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DMGB64_APP=ON \
    -DPORT_VALIDATION_TESTS=OFF \
    -DBUILD_TESTING=OFF \
    > "$BUILD_DIR-configure.log" 2>&1 || { cat "$BUILD_DIR-configure.log"; fail "cmake configure failed"; }

# --- 4. Build ge007.exe --------------------------------------------------------
echo "building ge007 (-j$JOBS) ..."
BUILD_LOG="$BUILD_DIR-build.log"
set +e
cmake --build "$BUILD_DIR" --target ge007 -j"$JOBS" > "$BUILD_LOG" 2>&1
RC=$?
set -e

WARN_COUNT="$(grep -c -iE 'warning:' "$BUILD_LOG" 2>/dev/null || true)"
if [ "$RC" != "0" ]; then
    echo "--- last 40 lines of build log ---"
    tail -40 "$BUILD_LOG"
    fail "compile/link error (see $BUILD_LOG)"
fi
[ -f "$ARTIFACT" ] || fail "build reported success but $ARTIFACT is missing"

# --- 4b. Struct-layout lock (S-tier Task 0.7, FID-0035/0036) -------------------
# This lane IS the drift scenario the layout asserts guard: MinGW defaults to
# -mms-bitfields, which repacked StandTile and crashed v0.3.2 on Windows. The
# main build above runs with BUILD_TESTING=OFF, so the struct_layout ctest is
# not built; compile-check it directly here (same layout-critical flags as the
# ge007 target) so any Windows-side layout drift fails this lane at compile time.
STRUCT_TEST="$REPO_ROOT/tests/test_struct_layout.c"
if [ -f "$STRUCT_TEST" ]; then
    echo "struct-layout lock (-fsyntax-only, MinGW) ..."
    "${CROSS_PREFIX:-x86_64-w64-mingw32}"-gcc -fsyntax-only \
        -DNONMATCHING -DNATIVE_PORT -DPORT_FIXME_STUBS -D_LANGUAGE_C \
        -DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG \
        -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DBYTEMATCH -DSDL_MAIN_HANDLED \
        -w -fms-extensions -mno-ms-bitfields \
        -I"$REPO_ROOT/include" -I"$REPO_ROOT/include/PR" -I"$REPO_ROOT/src" \
        -I"$REPO_ROOT/src/game" -I"$REPO_ROOT/src/platform" \
        -I"$REPO_ROOT/src/platform/fast3d" -I"$REPO_ROOT/src/libultra/audio" \
        -I"$REPO_ROOT/assets" -I"$REPO_ROOT/lib/glad/include" \
        -I"$REPO_ROOT/lib/stb" -I"$REPO_ROOT" \
        "$STRUCT_TEST" || fail "struct-layout asserts drifted under MinGW (see tests/test_struct_layout.c)"
    echo "struct-layout lock: PASS"
fi

# --- 5. Report -----------------------------------------------------------------
echo ""
echo "warnings: ${WARN_COUNT:-0} (non-fatal, matching CI posture; full log: $BUILD_LOG)"
if command -v file >/dev/null 2>&1; then echo "artifact: $(file "$ARTIFACT")"; else echo "artifact: $ARTIFACT"; fi
echo ""

# Import-table guard: the exe must be self-contained on stock Windows — only
# Windows-system DLLs and the SDL2.dll we ship beside it. A toolchain change
# that reintroduces libgcc_s_seh-1/libstdc++-6/libwinpthread-1 (present on the
# build machine, absent on stock Windows) fails here instead of on a player's
# machine ("The code execution cannot proceed because ... was not found").
BAD_IMPORTS="$("${CROSS_PREFIX:-x86_64-w64-mingw32}"-objdump -p "$ARTIFACT" | awk '/DLL Name/{print $3}' \
    | grep -viE '^(KERNEL32.dll|USER32.dll|SHELL32.dll|ole32.dll|GDI32.dll|ADVAPI32.dll|WS2_32.dll|IMM32.dll|OLEAUT32.dll|SETUPAPI.dll|VERSION.dll|WINMM.dll|SDL2.dll|api-ms-win-.*)$' || true)"
if [[ -n "$BAD_IMPORTS" ]]; then
    echo "MINGW CROSS-CHECK: FAIL — non-system DLL imports (would not run on stock Windows):" >&2
    echo "$BAD_IMPORTS" >&2
    exit 1
fi

echo "MINGW CROSS-CHECK: PASS — $ARTIFACT"
