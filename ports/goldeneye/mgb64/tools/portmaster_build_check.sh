#!/usr/bin/env bash
#
# portmaster_build_check.sh -- keep the PortMaster / GLES3 ARM target (PR #25 by
# jsmolina, repaired in c486be2) configuring + compiling, so it doesn't silently
# rot while the three desktop platforms move.
#
# The real target is a Linux ARM handheld (R36S / RG353* etc.): GLES 3.2
# (GLES3/gl32.h) + EGL + SDL2, driven by MGB64_PORTMASTER_GLES=ON. The buildable
# runtime config is the bare engine (MGB64_APP=OFF): the engine's own
# gfx_opengl.c GLES path needs no glad/ImGui, and pm-Goldeneye007.sh boots
# `./ge007 --rom ...` with MGB64_PORTMASTER=1 (640x480 fullscreen via
# platform_sdl.c).
#
# NOTE: MGB64_APP=ON does NOT build under GLES (the launcher's glad/ImGui wiring
# is unfinished for the GLES path); this lane deliberately builds MGB64_APP=OFF
# and must not flip it on. The desktop release (release.yml) builds MGB64_APP=ON
# on desktop GL only.
#
# LAYERED, LOCAL-FIRST -- it runs the deepest lane the host supports:
#
#   Phase 1  STRUCTURAL (always)    grep-level wiring invariants in CMakeLists +
#                                   the GLES sources. Encodes exactly the
#                                   regressions c486be2 repaired (x86-guarded
#                                   -mno-ms-bitfields, un-renamed macOS bundle
#                                   targets, no bare per-platform pthread) plus
#                                   PR #25's GLES branch surface. Fails HARD on
#                                   regression, on any host.
#   Phase 2  CONFIGURE (always)     `cmake -DMGB64_PORTMASTER_GLES=ON` parses +
#                                   configures. Catches CMakeLists syntax /
#                                   reachable-target breakage. (On macOS the
#                                   APPLE link branch is selected, so this is a
#                                   parse/configure sanity check, not the GLES
#                                   link path -- see Phase 3 + the CI job.)
#   Phase 3  COMPILE+LINK (deep)    A real GLES configure+compile+link of ge007.
#                                   Requires a GLES toolchain: a Linux host with
#                                   GLES/EGL/SDL2 dev packages (what the release
#                                   CI portmaster job provides), an
#                                   aarch64-linux-gnu cross toolchain + sysroot,
#                                   or a running Docker daemon (arm64 preferred
#                                   -- the true target arch, native on Apple
#                                   Silicon). Absent all three, Phase 3 is
#                                   skipped (documented), and the authoritative
#                                   compile+link happens in CI
#                                   (.github/workflows/release.yml -> portmaster).
#
# Exit: 0 = every lane that RAN passed. 1 = a lane failed (regression). 125 =
# SKIP (only with --require-deep, when no GLES toolchain is available).
#
# Usage: tools/portmaster_build_check.sh [--deep] [--require-deep] [--jobs N]
#                                        [--build-dir DIR]
#   --deep          Attempt Phase 3 even if a toolchain probe is ambiguous.
#   --require-deep  Phase 3 must run; exit 125 (skip) if no toolchain is present.
#   --jobs N        Parallelism for the deep build (default 8).
#   --build-dir DIR Scratch build dir (default /tmp/mgb64-relmech-build).
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

JOBS=8
BUILD_DIR="/tmp/mgb64-relmech-build"
WANT_DEEP=0
REQUIRE_DEEP=0
while [ $# -gt 0 ]; do
  case "$1" in
    --deep) WANT_DEEP=1 ;;
    --require-deep) WANT_DEEP=1; REQUIRE_DEEP=1 ;;
    --jobs) JOBS="$2"; shift ;;
    --jobs=*) JOBS="${1#*=}" ;;
    --build-dir) BUILD_DIR="$2"; shift ;;
    --build-dir=*) BUILD_DIR="${1#*=}" ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

fail=0
note_fail() { printf '  \033[31m[FAIL]\033[0m %s\n' "$1"; fail=1; }
ok()        { printf '  \033[32m[ok]\033[0m   %s\n' "$1"; }

CML="CMakeLists.txt"

echo "== PortMaster/GLES check =="
echo "repo: $REPO_ROOT"

# --- Phase 1: structural wiring invariants -----------------------------------
echo
echo "-- Phase 1: structural wiring (CMake + GLES sources) --"

want() {  # regex label file
  if grep -Eq -- "$1" "$3"; then ok "$2"; else note_fail "$2 (missing /$1/ in $3)"; fi
}
forbid() {  # fixed-string label file
  if grep -Fq -- "$1" "$3"; then note_fail "$2 (found forbidden '$1' in $3)"; else ok "$2"; fi
}

want 'option\(MGB64_PORTMASTER_GLES ' 'MGB64_PORTMASTER_GLES option declared' "$CML"
want 'if\(NOT APPLE AND NOT MGB64_PORTMASTER_GLES\)' 'glad loader excluded under GLES' "$CML"
want 'elseif\(MGB64_PORTMASTER_GLES\)' 'GLES link branch present' "$CML"
want '(^|[^A-Za-z_])GLESv2([^A-Za-z_]|$)' 'GLES branch links GLESv2' "$CML"
want '(^|[^A-Za-z_])EGL([^A-Za-z_]|$)' 'GLES branch links EGL' "$CML"
want 'MGB64_PORTMASTER_GLES MGB64_PORTMASTER' 'GLES branch sets PORTMASTER defs' "$CML"
# c486be2 repair invariants:
want 'CMAKE_SYSTEM_PROCESSOR MATCHES.*x86' '-mno-ms-bitfields x86-guarded (bitfield crash fix)' "$CML"
want '\$\{MGB64_MS_BITFIELD_FLAG\}' 'ge007 consumes the guarded bitfield flag' "$CML"
# The exact broken spellings PR #25 introduced and c486be2 removed:
forbid 'ge007 pthread_lib' 'macOS bundle target not renamed to "ge007 pthread_lib"' "$CML"
forbid 'target_link_libraries(ge007 pthread' 'ge007 link line has no bare stray "pthread"' "$CML"

want '#ifdef MGB64_PORTMASTER_GLES' 'gfx_opengl.c: GLES include branch' src/platform/fast3d/gfx_opengl.c
want '<GLES3/gl32\.h>' 'gfx_opengl.c: includes GLES3/gl32.h' src/platform/fast3d/gfx_opengl.c
want '#version 320 es' 'gfx_opengl.c: GLES shader version' src/platform/fast3d/gfx_opengl.c
want 'defined\(MGB64_PORTMASTER_GLES\)' 'app_host.cpp: GLES context attributes' src/app/app_host.cpp
want 'MGB64_PORTMASTER' 'main_app.cpp: PortMaster boot path' src/app/main_app.cpp
if [ -f pm-Goldeneye007.sh ]; then
  want 'MGB64_PORTMASTER' 'pm-Goldeneye007.sh: launcher exports MGB64_PORTMASTER' pm-Goldeneye007.sh
else
  note_fail 'pm-Goldeneye007.sh launcher present'
fi

if [ "$fail" -ne 0 ]; then
  echo
  echo "PortMaster/GLES check: FAIL (structural wiring regressed) -- see [FAIL] lines above."
  exit 1
fi
echo "  structural wiring: OK"

# --- Phase 2: host configure -------------------------------------------------
echo
echo "-- Phase 2: cmake configure (MGB64_PORTMASTER_GLES=ON) --"
CONFIG_DIR="${BUILD_DIR}-configure"
rm -rf "$CONFIG_DIR"
if cmake -S "$REPO_ROOT" -B "$CONFIG_DIR" \
      -DMGB64_PORTMASTER_GLES=ON -DMGB64_APP=OFF \
      -DBUILD_TESTING=OFF -DPORT_VALIDATION_TESTS=OFF \
      > "$CONFIG_DIR.log" 2>&1; then
  ok "configure succeeded (build system generated)"
  if [ "$(uname -s)" = "Darwin" ]; then
    echo "  note: on macOS the APPLE link branch is selected; the GLES link path"
    echo "        is validated by Phase 3 / the CI portmaster job, not here."
  fi
else
  tail -30 "$CONFIG_DIR.log" || true
  note_fail "cmake configure failed (see $CONFIG_DIR.log)"
  echo
  echo "PortMaster/GLES check: FAIL (configure)"
  exit 1
fi

# --- Phase 3: deep compile + link (conditional) ------------------------------
echo
echo "-- Phase 3: real GLES compile + link (deep) --"

deep_backend=""
# 1) Native Linux host with GLES/EGL dev headers (what the CI job provides).
if [ "$(uname -s)" = "Linux" ] && \
   { [ -e /usr/include/GLES3/gl32.h ] || [ -e /usr/include/GLES3/gl3.h ]; }; then
  deep_backend="native-linux-gles"
# 2) aarch64 cross toolchain.
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
  deep_backend="aarch64-cross"
# 3) Docker daemon reachable (arm64 = real target arch, native on Apple Silicon).
elif command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  deep_backend="docker-arm64"
fi

if [ -z "$deep_backend" ]; then
  echo "  no local GLES toolchain (no Linux GLES headers, no aarch64-linux-gnu-gcc,"
  echo "  no running Docker daemon)."
  echo "  A full compile+link needs one of:"
  echo "    - a Linux host with: libgles2-mesa-dev libegl1-mesa-dev libsdl2-dev libdbus-1-dev"
  echo "    - an aarch64-linux-gnu cross toolchain + GLES/EGL/SDL2 sysroot"
  echo "    - a running Docker daemon (arm64 image; native on Apple Silicon)"
  echo "  The authoritative compile+link runs in CI: .github/workflows/release.yml (portmaster job)."
  if [ "$REQUIRE_DEEP" -eq 1 ]; then
    echo
    echo "PortMaster/GLES check: SKIP deep compile (--require-deep, no toolchain)."
    exit 125
  fi
  echo
  echo "PortMaster/GLES check: PASS (structural + configure). Deep compile delegated to CI."
  exit 0
fi

echo "  deep backend: $deep_backend"
DEEP_DIR="${BUILD_DIR}"
deep_build_native() {  # runs on a Linux host / cross env with GLES headers
  rm -rf "$DEEP_DIR"
  cmake -S "$REPO_ROOT" -B "$DEEP_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DMGB64_PORTMASTER_GLES=ON -DMGB64_APP=OFF \
      -DBUILD_TESTING=OFF -DPORT_VALIDATION_TESTS=OFF "$@" \
      > "$DEEP_DIR-configure.log" 2>&1 || { tail -40 "$DEEP_DIR-configure.log"; return 1; }
  cmake --build "$DEEP_DIR" --target ge007 -j"$JOBS" > "$DEEP_DIR-build.log" 2>&1 || { tail -60 "$DEEP_DIR-build.log"; return 1; }
  [ -f "$DEEP_DIR/ge007" ]
}

case "$deep_backend" in
  native-linux-gles)
    if deep_build_native; then ok "GLES compile+link OK ($DEEP_DIR/ge007)"; else note_fail "GLES compile+link failed"; fi
    ;;
  aarch64-cross)
    if [ -f cmake/aarch64-linux-gnu.cmake ]; then
      if deep_build_native -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/cmake/aarch64-linux-gnu.cmake"; then
        ok "GLES cross compile+link OK ($DEEP_DIR/ge007)"
      else note_fail "GLES cross compile+link failed"; fi
    else
      echo "  aarch64 toolchain present but cmake/aarch64-linux-gnu.cmake is absent;"
      echo "  add that toolchain file (SDL2/GLES/EGL sysroot) to enable this backend."
      [ "$REQUIRE_DEEP" -eq 1 ] && { echo "PortMaster/GLES check: SKIP deep (no cross toolchain file)."; exit 125; }
      echo "PortMaster/GLES check: PASS (structural + configure)."; exit 0
    fi
    ;;
  docker-arm64)
    IMG="debian:bookworm-slim"
    echo "  building in $IMG (linux/arm64) -- installs cmake/gcc/GLES/EGL/SDL2, then builds ge007"
    if docker run --rm --platform linux/arm64 -v "$REPO_ROOT":/src:ro -w /work "$IMG" bash -c '
        set -e
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq cmake g++ pkg-config \
          libgles2-mesa-dev libegl1-mesa-dev libsdl2-dev libdbus-1-dev >/dev/null
        cp -a /src /work/repo
        cmake -S /work/repo -B /work/build -DCMAKE_BUILD_TYPE=Release \
          -DMGB64_PORTMASTER_GLES=ON -DMGB64_APP=OFF \
          -DBUILD_TESTING=OFF -DPORT_VALIDATION_TESTS=OFF
        cmake --build /work/build --target ge007 -j'"$JOBS"'
        test -f /work/build/ge007
      '; then
      ok "GLES compile+link OK (docker linux/arm64)"
    else
      note_fail "GLES compile+link failed (docker linux/arm64)"
    fi
    ;;
esac

echo
if [ "$fail" -ne 0 ]; then
  echo "PortMaster/GLES check: FAIL (deep compile+link)."
  exit 1
fi
echo "PortMaster/GLES check: PASS (structural + configure + deep compile+link)."
