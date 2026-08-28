#!/bin/bash
# build_universal.sh — Build Universal C engine library (arm64 + x86_64)
#
# Compiles the project via CMake for both Apple Silicon and Intel architectures,
# producing a single universal libge007_lib.a for the macOS app shell. This
# does not build, sign, notarize, or package a `.app`; use build_app_bundle.sh
# for the local unsigned app bundle.
#
# Usage: ./build_universal.sh [options]
#   --release              Build with Release optimizations (default)
#   --debug                Build with Debug symbols and no optimization
#   --build-dir PATH       CMake build directory (default: build-macos)
#   --deployment-target V  Minimum macOS version (default: 13.0)

set -euo pipefail

# ---------------------------------------------------------------------------
# Color helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info()    { printf "${GREEN}[INFO]${NC}  %s\n" "$*"; }
warn()    { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
error()   { printf "${RED}[ERROR]${NC} %s\n" "$*" >&2; }

die() {
    error "$@"
    exit 1
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
BUILD_TYPE="Release"
BUILD_DIR=""
DEPLOYMENT_TARGET="13.0"

usage() {
    cat <<'EOF'
Usage: macos/Scripts/build_universal.sh [options]

Builds the universal arm64 + x86_64 C engine static library for the macOS app
shell. This does not build, sign, notarize, or package a .app.

Options:
  --release              Build with Release optimizations (default)
  --debug                Build with Debug symbols and no optimization
  --build-dir PATH       CMake build directory (default: build-macos)
  --deployment-target V  Minimum macOS version (default: 13.0)
  -h, --help             Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release) BUILD_TYPE="Release"; shift ;;
        --debug)   BUILD_TYPE="Debug";   shift ;;
        --build-dir)
            [[ $# -ge 2 ]] || die "--build-dir requires a path"
            BUILD_DIR="$2"
            shift 2
            ;;
        --deployment-target)
            [[ $# -ge 2 ]] || die "--deployment-target requires a version"
            DEPLOYMENT_TARGET="$2"
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *)         usage >&2; die "Unknown argument: $1" ;;
    esac
done

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
if [[ "${BUILD_DIR}" != /* ]]; then
    BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR:-build-macos}"
fi

info "Project root : ${PROJECT_ROOT}"
info "Build dir    : ${BUILD_DIR}"
info "Build type   : ${BUILD_TYPE}"
info "Target       : ${DEPLOYMENT_TARGET}"

# ---------------------------------------------------------------------------
# Check required tools
# ---------------------------------------------------------------------------
for tool in cmake clang lipo; do
    if ! command -v "$tool" &>/dev/null; then
        die "Required tool '${tool}' not found. Please install it before continuing."
    fi
done

info "cmake  : $(cmake --version | head -1)"
info "clang  : $(clang --version | head -1)"

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
info "Configuring CMake..."

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DMACOS_APP_BUNDLE=ON \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    || die "CMake configuration failed."

info "Configuration complete."

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
NCPU="$(sysctl -n hw.ncpu)"
info "Building ge007_lib with ${NCPU} parallel jobs..."

cmake --build "${BUILD_DIR}" --target ge007_lib --parallel "${NCPU}" \
    || die "ge007_lib build failed. Check the output above for details."

info "Build complete."

# ---------------------------------------------------------------------------
# Locate the universal library and print summary
# ---------------------------------------------------------------------------
LIBRARY="${BUILD_DIR}/libge007_lib.a"

if [[ ! -f "${LIBRARY}" ]]; then
    die "Expected library not found: ${LIBRARY}"
fi

echo ""
info "========== Build Summary =========="
info "Library      : ${LIBRARY}"
info "Architectures: $(lipo -info "${LIBRARY}" 2>/dev/null || file "${LIBRARY}")"
info "Size         : $(du -h "${LIBRARY}" | cut -f1)"
info "Verify assets: ${PROJECT_ROOT}/macos/Scripts/verify_asset_free.sh '${LIBRARY}'"
info "==================================="

info "Done."
