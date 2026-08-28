#!/usr/bin/env bash
#
# build_app_bundle.sh -- Build a local unsigned MGB64.app bundle.
#
# This is the repeatable maintainer/developer packaging path. It intentionally
# does not sign, notarize, create a DMG, or bundle game data. Users still bring
# their own ROM at runtime.
#
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { printf "${GREEN}[INFO]${NC}  %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
error() { printf "${RED}[ERROR]${NC} %s\n" "$*" >&2; }

die() {
    error "$@"
    exit 1
}

version_gt() {
    local lhs="$1"
    local rhs="$2"
    local lhs_parts rhs_parts
    IFS=. read -r -a lhs_parts <<< "${lhs}"
    IFS=. read -r -a rhs_parts <<< "${rhs}"

    for i in 0 1 2; do
        local l="${lhs_parts[$i]:-0}"
        local r="${rhs_parts[$i]:-0}"
        if ((10#$l > 10#$r)); then
            return 0
        fi
        if ((10#$l < 10#$r)); then
            return 1
        fi
    done

    return 1
}

sdl2_dylib_path() {
    local libdir
    libdir="$(pkg-config --variable=libdir sdl2 2>/dev/null || true)"
    [[ -n "${libdir}" ]] || return 1

    for name in libSDL2-2.0.0.dylib libSDL2.dylib; do
        if [[ -f "${libdir}/${name}" ]]; then
            printf "%s\n" "${libdir}/${name}"
            return 0
        fi
    done

    return 1
}

macos_dylib_minos() {
    local dylib="$1"
    otool -l "${dylib}" 2>/dev/null | awk '
        $1 == "cmd" && $2 == "LC_BUILD_VERSION" {
            in_build_version = 1
            in_version_min = 0
            next
        }
        in_build_version && $1 == "minos" {
            print $2
            exit
        }
        $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" {
            in_version_min = 1
            in_build_version = 0
            next
        }
        in_version_min && $1 == "version" {
            print $2
            exit
        }
    '
}

usage() {
    cat <<'EOF'
Usage: macos/Scripts/build_app_bundle.sh [options]

Builds the C engine library and Swift/AppKit shell, then assembles an unsigned
local macOS app bundle.

Options:
  --release              Build with Release optimizations (default)
  --debug                Build with Debug settings
  --build-dir PATH       CMake build directory (default: build-macos)
  --output PATH          Output .app path (default: <build-dir>/MGB64.app)
  --arch ARCH            Build one architecture: native, arm64, or x86_64
                         (default: native)
  --deployment-target V  Minimum macOS version (default: 13.0)
  --strict-deployment-target
                         Fail if the local SDL2 dylib requires a newer macOS
                         version than --deployment-target.
  --bundle-sdl2          Copy the linked SDL2 dylib into Contents/Frameworks
                         and rewrite the executable load path to the bundle.
  --no-cmake             Reuse an existing build-macos/libge007_lib.a
  -h, --help             Show this help

By default the resulting bundle is unsigned and still depends on SDL2 being
available at the path reported by pkg-config. For distributable build
candidates, use --strict-deployment-target --bundle-sdl2, then sign,
notarize, and package the bundle with the separate scripts in macos/Scripts/.
EOF
}

BUILD_TYPE="Release"
BUILD_DIR=""
OUTPUT_APP=""
ARCH="native"
DEPLOYMENT_TARGET="13.0"
STRICT_DEPLOYMENT_TARGET=false
BUNDLE_SDL2=false
RUN_CMAKE=true
APP_NAME="MGB64"
EXECUTABLE_NAME="MGB64"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release) BUILD_TYPE="Release"; shift ;;
        --debug) BUILD_TYPE="Debug"; shift ;;
        --build-dir)
            [[ $# -ge 2 ]] || die "--build-dir requires a path"
            BUILD_DIR="$2"
            shift 2
            ;;
        --output)
            [[ $# -ge 2 ]] || die "--output requires a path"
            OUTPUT_APP="$2"
            shift 2
            ;;
        --arch)
            [[ $# -ge 2 ]] || die "--arch requires native, arm64, or x86_64"
            ARCH="$2"
            shift 2
            ;;
        --deployment-target)
            [[ $# -ge 2 ]] || die "--deployment-target requires a version"
            DEPLOYMENT_TARGET="$2"
            shift 2
            ;;
        --strict-deployment-target) STRICT_DEPLOYMENT_TARGET=true; shift ;;
        --bundle-sdl2) BUNDLE_SDL2=true; shift ;;
        --no-cmake) RUN_CMAKE=false; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown argument: $1. Use --help." ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [[ "${BUILD_DIR}" != /* ]]; then
    BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR:-build-macos}"
fi
if [[ -z "${OUTPUT_APP}" ]]; then
    OUTPUT_APP="${BUILD_DIR}/${APP_NAME}.app"
elif [[ "${OUTPUT_APP}" != /* ]]; then
    OUTPUT_APP="${PROJECT_ROOT}/${OUTPUT_APP}"
fi

case "${ARCH}" in
    native) CMAKE_ARCH="$(uname -m)" ;;
    arm64|x86_64) CMAKE_ARCH="${ARCH}" ;;
    *) die "--arch must be native, arm64, or x86_64" ;;
esac

for tool in cmake swiftc pkg-config plutil ditto iconutil python3 /usr/libexec/PlistBuddy; do
    if ! command -v "$tool" &>/dev/null; then
        die "Required tool '${tool}' not found."
    fi
done
if [[ "${BUNDLE_SDL2}" == true ]] && ! command -v install_name_tool &>/dev/null; then
    die "Required tool 'install_name_tool' not found."
fi

if ! pkg-config --exists sdl2; then
    die "SDL2 was not found by pkg-config. Install it with: brew install sdl2"
fi

REQUESTED_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
SDL2_DYLIB="$(sdl2_dylib_path || true)"
SDL2_MINOS=""
if [[ "${STRICT_DEPLOYMENT_TARGET}" == true && -z "${SDL2_DYLIB}" ]]; then
    die "Could not resolve the SDL2 dylib from pkg-config; cannot enforce --strict-deployment-target."
fi
if [[ -n "${SDL2_DYLIB}" ]]; then
    SDL2_MINOS="$(macos_dylib_minos "${SDL2_DYLIB}")"
    if [[ "${STRICT_DEPLOYMENT_TARGET}" == true && -z "${SDL2_MINOS}" ]]; then
        die "Could not determine the SDL2 dylib minimum macOS version; cannot enforce --strict-deployment-target."
    fi
    if [[ -n "${SDL2_MINOS}" ]] && version_gt "${SDL2_MINOS}" "${DEPLOYMENT_TARGET}"; then
        if [[ "${STRICT_DEPLOYMENT_TARGET}" == true ]]; then
            die "SDL2 dylib requires macOS ${SDL2_MINOS}, newer than requested target ${DEPLOYMENT_TARGET}. Use an SDL2 build with a compatible minimum deployment target, or omit --strict-deployment-target for local-only builds."
        else
            warn "SDL2 dylib requires macOS ${SDL2_MINOS}; raising local bundle target from ${DEPLOYMENT_TARGET}."
            DEPLOYMENT_TARGET="${SDL2_MINOS}"
        fi
    fi
fi
if [[ "${BUNDLE_SDL2}" == true && -z "${SDL2_DYLIB}" ]]; then
    die "Could not resolve the SDL2 dylib from pkg-config; cannot bundle SDL2."
fi

info "Project root      : ${PROJECT_ROOT}"
info "Build dir         : ${BUILD_DIR}"
info "Output app        : ${OUTPUT_APP}"
info "Build type        : ${BUILD_TYPE}"
info "Architecture      : ${CMAKE_ARCH}"
if [[ "${REQUESTED_DEPLOYMENT_TARGET}" != "${DEPLOYMENT_TARGET}" ]]; then
    info "Requested target  : ${REQUESTED_DEPLOYMENT_TARGET}"
fi
info "Deployment target : ${DEPLOYMENT_TARGET}"
if [[ -n "${SDL2_DYLIB}" ]]; then
    info "SDL2 dylib        : ${SDL2_DYLIB}${SDL2_MINOS:+ (min macOS ${SDL2_MINOS})}"
fi
if [[ "${STRICT_DEPLOYMENT_TARGET}" == true ]]; then
    info "Strict target     : enabled"
fi
if [[ "${BUNDLE_SDL2}" == true ]]; then
    info "Bundle SDL2       : enabled"
fi

if [[ "${RUN_CMAKE}" == true ]]; then
    info "Configuring C engine library..."
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
        -DMACOS_APP_BUNDLE=ON \
        -DCMAKE_OSX_ARCHITECTURES="${CMAKE_ARCH}" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        || die "CMake configuration failed."

    NCPU="$(sysctl -n hw.ncpu)"
    info "Building ge007_lib with ${NCPU} parallel jobs..."
    cmake --build "${BUILD_DIR}" --target ge007_lib --parallel "${NCPU}" \
        || die "ge007_lib build failed."
fi

ENGINE_LIB="${BUILD_DIR}/libge007_lib.a"
if [[ ! -f "${ENGINE_LIB}" ]]; then
    die "Missing engine library: ${ENGINE_LIB}. Run without --no-cmake first."
fi

# WebGPU (wgpu-native) transitive link deps. libge007_lib.a is a STATIC archive,
# so it carries the engine's wgpu*/C++ symbol REFERENCES but not the wgpu library
# or libc++ themselves — the final swiftc link must resolve them. Since the
# 2026-07-13 WebGPU-default flip, MGB64_WEBGPU_BACKEND=ON is the default, so
# ge007_lib references wgpuCreateInstance/etc. and needs these; without them the
# app-bundle link fails "Undefined symbols … _wgpu* / ___gxx_personality_v0".
# The lib path + macOS framework list mirror cmake/webgpu.cmake exactly (the
# INTERFACE `webgpu` target the ge007 executable links). Empty when the wgpu lib
# is absent (a GL/Metal-only MGB64_WEBGPU_BACKEND=OFF build), so that path is
# unaffected.
WGPU_LINK=()
WGPU_LIB="${BUILD_DIR}/_deps/wgpu_native-src/lib/libwgpu_native.a"
if [[ -f "${WGPU_LIB}" ]]; then
    WGPU_LINK=(
        "${WGPU_LIB}"
        -lc++
        -framework CoreFoundation
        -framework IOKit
        -framework IOSurface
        -framework CoreGraphics
        -framework Security
    )
fi

SWIFT_SOURCES=()
while IFS= read -r source; do
    SWIFT_SOURCES+=("${source}")
done < <(find "${PROJECT_ROOT}/macos/Sources" -maxdepth 1 -name '*.swift' -print | sort)

if [[ "${#SWIFT_SOURCES[@]}" -eq 0 ]]; then
    die "No Swift sources found under macos/Sources"
fi

SDL_CFLAGS=()
while IFS= read -r flag; do
    [[ -n "${flag}" ]] && SDL_CFLAGS+=("${flag}")
done < <(pkg-config --cflags sdl2 | tr ' ' '\n')

SDL_LIBS=()
while IFS= read -r flag; do
    [[ -z "${flag}" ]] && continue
    # swiftc (used for the app shell below) does NOT accept clang's `-Wl,a,b`
    # linker-passthrough form. Homebrew SDL2's `pkg-config --libs sdl2` emits
    # e.g. `-Wl,-framework,Cocoa`, which made swiftc die "unknown argument".
    # Rewrite each `-Wl,`-prefixed flag into swiftc's `-Xlinker <arg>` form
    # (one -Xlinker per comma-separated arg). Flags with no `-Wl,` (e.g. -lSDL2,
    # -L...) pass through unchanged, so SDL builds whose --libs carry no -Wl,
    # (many do not) are unaffected.
    case "${flag}" in
        -Wl,*)
            IFS=',' read -ra _wl_parts <<< "${flag#-Wl,}"
            for _wl_p in "${_wl_parts[@]}"; do
                [[ -n "${_wl_p}" ]] && SDL_LIBS+=("-Xlinker" "${_wl_p}")
            done
            ;;
        *)
            SDL_LIBS+=("${flag}")
            ;;
    esac
done < <(pkg-config --libs sdl2 | tr ' ' '\n')

rm -rf "${OUTPUT_APP}"
mkdir -p "${OUTPUT_APP}/Contents/MacOS" "${OUTPUT_APP}/Contents/Resources"

INFO_PLIST="${OUTPUT_APP}/Contents/Info.plist"
cp "${PROJECT_ROOT}/macos/Resources/Info.plist" "${INFO_PLIST}"
/usr/libexec/PlistBuddy -c "Set :CFBundleExecutable ${EXECUTABLE_NAME}" "${INFO_PLIST}"
/usr/libexec/PlistBuddy -c "Set :LSMinimumSystemVersion ${DEPLOYMENT_TARGET}" "${INFO_PLIST}"
plutil -lint "${INFO_PLIST}" >/dev/null

for resource in Localizable.xcstrings PrivacyInfo.xcprivacy; do
    if [[ -f "${PROJECT_ROOT}/macos/Resources/${resource}" ]]; then
        ditto "${PROJECT_ROOT}/macos/Resources/${resource}" \
            "${OUTPUT_APP}/Contents/Resources/${resource}"
    fi
done

ICONSET_DIR="${BUILD_DIR}/AppIcon.iconset"
APP_ICON="${OUTPUT_APP}/Contents/Resources/AppIcon.icns"
ICON_SOURCE="${PROJECT_ROOT}/branding/appicon-source.png"
ICON_SOURCE_ARGS=()
if [[ -f "${ICON_SOURCE}" ]]; then
    ICON_SOURCE_ARGS=(--source "${ICON_SOURCE}")
else
    warn "branding/appicon-source.png not found; using the placeholder procedural icon."
fi
info "Generating app icon..."
python3 "${PROJECT_ROOT}/macos/Scripts/generate_app_icon.py" \
    --iconset "${ICONSET_DIR}" \
    --icns "${APP_ICON}" \
    "${ICON_SOURCE_ARGS[@]}" \
    || die "App icon generation failed."
[[ -s "${APP_ICON}" ]] || die "Generated app icon is missing: ${APP_ICON}"

EXECUTABLE_PATH="${OUTPUT_APP}/Contents/MacOS/${EXECUTABLE_NAME}"

info "Compiling Swift app shell..."
swiftc -o "${EXECUTABLE_PATH}" \
    -target "${CMAKE_ARCH}-apple-macosx${DEPLOYMENT_TARGET}" \
    -import-objc-header "${PROJECT_ROOT}/macos/Sources/MGB64-Bridging-Header.h" \
    -I "${PROJECT_ROOT}/macos/Sources" \
    "${SWIFT_SOURCES[@]}" \
    "${ENGINE_LIB}" \
    "${WGPU_LINK[@]}" \
    "${SDL_CFLAGS[@]}" \
    "${SDL_LIBS[@]}" \
    -framework Cocoa \
    -framework SwiftUI \
    -framework GameController \
    -framework UniformTypeIdentifiers \
    -framework UserNotifications \
    -framework QuartzCore \
    -framework CoreVideo \
    -framework Metal \
    -framework OpenGL \
    || die "Swift app shell build failed."

chmod +x "${EXECUTABLE_PATH}"

SDL2_BUNDLED_PATH=""
SDL2_BUNDLE_LOAD_PATH=""
if [[ "${BUNDLE_SDL2}" == true ]]; then
    FRAMEWORKS_DIR="${OUTPUT_APP}/Contents/Frameworks"
    SDL2_BASENAME="$(basename "${SDL2_DYLIB}")"
    SDL2_BUNDLED_PATH="${FRAMEWORKS_DIR}/${SDL2_BASENAME}"
    SDL2_BUNDLE_LOAD_PATH="@executable_path/../Frameworks/${SDL2_BASENAME}"

    info "Bundling SDL2 dylib..."
    mkdir -p "${FRAMEWORKS_DIR}"
    ditto "${SDL2_DYLIB}" "${SDL2_BUNDLED_PATH}" \
        || die "Failed to copy SDL2 dylib into the app bundle."
    chmod u+w "${SDL2_BUNDLED_PATH}" 2>/dev/null || true

    SDL2_LOAD_PATHS=()
    while IFS= read -r load_path; do
        [[ -n "${load_path}" ]] && SDL2_LOAD_PATHS+=("${load_path}")
    done < <(
        otool -L "${EXECUTABLE_PATH}" \
            | awk '/libSDL2[^[:space:]]*\.dylib/ { print $1 }' \
            | sort -u
    )
    if [[ "${#SDL2_LOAD_PATHS[@]}" -eq 0 ]]; then
        die "The app executable does not link an SDL2 dylib; cannot rewrite bundle load path."
    fi
    for load_path in "${SDL2_LOAD_PATHS[@]}"; do
        install_name_tool -change "${load_path}" "${SDL2_BUNDLE_LOAD_PATH}" "${EXECUTABLE_PATH}" \
            || die "Failed to rewrite SDL2 load path: ${load_path}"
    done

    if ! otool -L "${EXECUTABLE_PATH}" | grep -Fq "${SDL2_BUNDLE_LOAD_PATH}"; then
        die "SDL2 bundle load path was not recorded in the app executable."
    fi
fi

echo "APPL????" > "${OUTPUT_APP}/Contents/PkgInfo"

echo ""
info "========== App Bundle Summary =========="
info "App bundle    : ${OUTPUT_APP}"
info "Executable    : ${EXECUTABLE_PATH}"
info "Architectures : $(lipo -info "${EXECUTABLE_PATH}" 2>/dev/null || file "${EXECUTABLE_PATH}")"
info "Bundle size   : $(du -sh "${OUTPUT_APP}" | cut -f1)"
info "App icon      : ${APP_ICON}"
info "SDL2 link     : $(otool -L "${EXECUTABLE_PATH}" | grep -E 'libSDL2' | sed 's/^[[:space:]]*//' || echo 'not found')"
if [[ -n "${SDL2_BUNDLED_PATH}" ]]; then
    info "SDL2 bundled  : ${SDL2_BUNDLED_PATH}"
fi
info "Verify assets : ${PROJECT_ROOT}/macos/Scripts/verify_asset_free.sh '${OUTPUT_APP}'"
info "========================================"

warn "This app is unsigned and not notarized. Use sign_and_notarize.sh only for distributable builds."
