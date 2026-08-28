#!/bin/bash
# run_tests.sh -- Build and run ROM validation tests.
#
# Compiles test_rom_validation.c + test_stubs.c + GameBridge.c into a
# standalone binary and runs it. Requires macOS (uses CommonCrypto).
#
# Exit code: 0 if all tests pass, 1 if any fail, 2 on build error.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../" && pwd)"
SOURCES_DIR="$SCRIPT_DIR/../Sources"
OUT="/tmp/test_rom_validation"

echo "=== Building ROM validation tests ==="
echo "  Project root: $PROJECT_ROOT"

cc \
    -DMACOS_APP_BUNDLE \
    -DNATIVE_PORT \
    -DNONMATCHING \
    -I"$SOURCES_DIR" \
    -I"$PROJECT_ROOT/include" \
    -I"$PROJECT_ROOT/include/PR" \
    -I"$PROJECT_ROOT/src" \
    -I"$PROJECT_ROOT/src/game" \
    -I"$PROJECT_ROOT/src/platform" \
    -I"$PROJECT_ROOT/src/platform/fast3d" \
    -I"$PROJECT_ROOT/src/libultra/gu" \
    -I"$PROJECT_ROOT/src/libultra" \
    -I"$PROJECT_ROOT/src/libultra/audio" \
    -I"$PROJECT_ROOT/src/libultrare/audio" \
    -I"$PROJECT_ROOT/assets" \
    -I"$PROJECT_ROOT" \
    -Wno-deprecated-declarations \
    -framework Security \
    -framework CoreFoundation \
    "$SCRIPT_DIR/test_rom_validation.c" \
    "$SCRIPT_DIR/test_stubs.c" \
    "$SOURCES_DIR/GameBridge.c" \
    -o "$OUT"

echo "=== Build OK ==="
echo ""

"$OUT"
EXIT_CODE=$?

# Clean up binary
rm -f "$OUT"

if [[ "$EXIT_CODE" -ne 0 ]]; then
    exit "$EXIT_CODE"
fi

echo ""
"$SCRIPT_DIR/test_asset_free_verifier.sh"
