#!/usr/bin/env bash
# build_gl_app.sh — package the portable in-process ImGui app (the GL `ge007`
# binary, MGB64_APP=ON) into a double-clickable, asset-free MGB64.app.
#
# Unlike build_app_bundle.sh (which wraps the macOS Swift shell + libge007_lib),
# this wraps the single cross-platform ge007 executable. Unsigned by default
# (ad-hoc codesign to reduce Gatekeeper friction); notarization stays deferred.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-macos-app"
OUTPUT_APP=""
UNIVERSAL=false
EXECUTABLE_NAME="ge007"
VERSION="dev"   # AUDIT-0053: release version, flowed into the binary + Info.plist

usage() {
    cat <<EOF
Usage: $(basename "$0") [--build-dir DIR] [--output APP] [--universal] [--version VER]
  --build-dir DIR   CMake build dir (default: build-macos-app)
  --output APP      Output .app path (default: <build-dir>/MGB64.app)
  --universal       Build a universal arm64+x86_64 binary (default: host arch)
  --version VER     Release version to embed (default: dev)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --output) OUTPUT_APP="$2"; shift 2 ;;
        --universal) UNIVERSAL=true; shift ;;
        --version) VERSION="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
    esac
done
[[ -n "${OUTPUT_APP}" ]] || OUTPUT_APP="${BUILD_DIR}/MGB64.app"

# AUDIT-0053: CFBundleShortVersionString must be 1-3 dot-separated integers. Strip
# a leading "v" and keep the leading numeric-dotted prefix; fall back to 0.0.0 for
# a non-release label like "dev".
APPLE_VERSION="$(printf '%s' "${VERSION#v}" | grep -oE '^[0-9]+(\.[0-9]+){0,2}' || true)"
[[ -n "${APPLE_VERSION}" ]] || APPLE_VERSION="0.0.0"

info() { echo "[build_gl_app] $*"; }
die()  { echo "[build_gl_app] ERROR: $*" >&2; exit 1; }

for tool in cmake python3 iconutil install_name_tool otool codesign /usr/libexec/PlistBuddy; do
    command -v "$tool" &>/dev/null || [[ -x "$tool" ]] || die "Required tool not found: $tool"
done

# --- 1. Build the GL app binary ---
ARCH_FLAG=()
if [[ "${UNIVERSAL}" == true ]]; then
    ARCH_FLAG=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")
    info "Building universal (arm64 + x86_64)"
fi
info "Configuring + building ge007 (MGB64_APP=ON, Release)..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DMGB64_APP=ON \
      -DPORT_VALIDATION_TESTS=OFF -DMGB64_VERSION="${VERSION}" \
      "${ARCH_FLAG[@]}" "${PROJECT_ROOT}" >/dev/null
cmake --build "${BUILD_DIR}" --target ge007 -j >/dev/null
BINARY="${BUILD_DIR}/ge007"
[[ -x "${BINARY}" ]] || die "ge007 binary not built at ${BINARY}"

# --- 2. Assemble the .app skeleton ---
info "Assembling ${OUTPUT_APP}"
rm -rf "${OUTPUT_APP}"
mkdir -p "${OUTPUT_APP}/Contents/MacOS" \
         "${OUTPUT_APP}/Contents/Resources" \
         "${OUTPUT_APP}/Contents/Frameworks"
cp "${BINARY}" "${OUTPUT_APP}/Contents/MacOS/${EXECUTABLE_NAME}"

# Community controller-mapping DB (MC.2). SDL_GetBasePath() returns
# Contents/Resources/ for a .app bundle, so the runtime loader finds it here.
cp "${PROJECT_ROOT}/lib/sdl_gamecontrollerdb/gamecontrollerdb.txt" \
   "${OUTPUT_APP}/Contents/Resources/gamecontrollerdb.txt"

# --- 3. Info.plist ---
INFO_PLIST="${OUTPUT_APP}/Contents/Info.plist"
cp "${PROJECT_ROOT}/macos/Resources/Info.plist" "${INFO_PLIST}"
/usr/libexec/PlistBuddy -c "Set :CFBundleExecutable ${EXECUTABLE_NAME}" "${INFO_PLIST}"
# Ensure a high-DPI-capable, non-transparent GL window is declared sane.
/usr/libexec/PlistBuddy -c "Set :CFBundleName MGB64" "${INFO_PLIST}" 2>/dev/null || true
# AUDIT-0053: stamp the release version so the bundle isn't shipped with a stale
# placeholder. Add the keys if the template lacks them.
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${APPLE_VERSION}" "${INFO_PLIST}" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string ${APPLE_VERSION}" "${INFO_PLIST}"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion ${APPLE_VERSION}" "${INFO_PLIST}" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :CFBundleVersion string ${APPLE_VERSION}" "${INFO_PLIST}"
info "Embedded version: ${VERSION} (CFBundleShortVersionString=${APPLE_VERSION})"

# --- 4. App icon (generated from branding/appicon-source.png via sips) ---
ICON_SOURCE="${PROJECT_ROOT}/branding/appicon-source.png"
ICON_SOURCE_ARGS=()
if [[ -f "${ICON_SOURCE}" ]]; then
    ICON_SOURCE_ARGS=(--source "${ICON_SOURCE}")
else
    info "branding/appicon-source.png not found; using the placeholder procedural icon."
fi
info "Generating app icon..."
ICONSET_DIR="$(mktemp -d)/AppIcon.iconset"
python3 "${PROJECT_ROOT}/macos/Scripts/generate_app_icon.py" \
    --iconset "${ICONSET_DIR}" \
    --icns "${OUTPUT_APP}/Contents/Resources/AppIcon.icns" \
    "${ICON_SOURCE_ARGS[@]}" \
    || die "App icon generation failed."

# --- 5. Bundle the linked SDL2 dylib ---
EXECUTABLE_PATH="${OUTPUT_APP}/Contents/MacOS/${EXECUTABLE_NAME}"
SDL2_LOAD="$(otool -L "${EXECUTABLE_PATH}" | awk '/libSDL2[^[:space:]]*\.dylib/ { print $1; exit }')"
if [[ -n "${SDL2_LOAD}" ]]; then
    # Resolve the real dylib path (handle @rpath by asking pkg-config/homebrew).
    SDL2_REAL="${SDL2_LOAD}"
    if [[ "${SDL2_LOAD}" == @* ]]; then
        SDL2_REAL="$(pkg-config --variable=libdir sdl2 2>/dev/null)/$(basename "${SDL2_LOAD}")"
    fi
    if [[ -f "${SDL2_REAL}" ]]; then
        SDL2_BASE="$(basename "${SDL2_REAL}")"
        cp -L "${SDL2_REAL}" "${OUTPUT_APP}/Contents/Frameworks/${SDL2_BASE}"
        chmod u+w "${OUTPUT_APP}/Contents/Frameworks/${SDL2_BASE}"
        install_name_tool -change "${SDL2_LOAD}" \
            "@executable_path/../Frameworks/${SDL2_BASE}" "${EXECUTABLE_PATH}"
        info "Bundled SDL2: ${SDL2_BASE}"
    else
        info "WARN: could not resolve SDL2 dylib (${SDL2_LOAD}); app will need a system SDL2."
    fi
fi

# --- 6. Ad-hoc sign (reduces Gatekeeper friction; not notarized) ---
info "Ad-hoc code signing..."
codesign --force --deep --sign - "${OUTPUT_APP}" >/dev/null 2>&1 || \
    info "WARN: ad-hoc codesign failed (bundle still runs via right-click > Open)."

# --- 7. Verify asset-free ---
info "Verifying asset-free..."
"${PROJECT_ROOT}/macos/Scripts/verify_asset_free.sh" "${OUTPUT_APP}"

info "Done: ${OUTPUT_APP}"
info "Run: open '${OUTPUT_APP}'   (first launch: right-click > Open to bypass Gatekeeper)"
