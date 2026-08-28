#!/bin/bash
#
# test_asset_free_verifier.sh -- Regression tests for verify_asset_free.sh.
#
# Builds a tiny synthetic macOS app bundle in /tmp. No ROM or game media is
# used. The goal is to prove the verifier accepts the current project-owned
# resource allowlist and rejects common bundle contamination mistakes.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TMPDIR="$(mktemp -d /tmp/mgb64_asset_verify.XXXXXX)"

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT INT TERM

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "SKIP: macOS app-bundle verifier tests require Darwin"
    exit 0
fi

for tool in cc python3 iconutil /usr/libexec/PlistBuddy; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "FAIL: required tool not found: $tool" >&2
        exit 2
    fi
done

VERIFY_SCRIPT="${PROJECT_ROOT}/macos/Scripts/verify_asset_free.sh"
ICON_SCRIPT="${PROJECT_ROOT}/macos/Scripts/generate_app_icon.py"
BASE_APP="${TMPDIR}/Clean.app"

make_clean_app() {
    local app="$1"
    local resources="${app}/Contents/Resources"
    local macos="${app}/Contents/MacOS"

    mkdir -p "$resources" "$macos"

    cat >"${TMPDIR}/main.c" <<'EOF'
int main(void) { return 0; }
EOF
    cc "${TMPDIR}/main.c" -o "${macos}/Clean"

    cat >"${app}/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>Clean</string>
  <key>CFBundleIconFile</key>
  <string>AppIcon</string>
  <key>CFBundleIdentifier</key>
  <string>com.mgb64.test.clean</string>
  <key>CFBundleName</key>
  <string>MGB64 verifier test</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
</dict>
</plist>
EOF

    cp "${PROJECT_ROOT}/macos/Resources/Localizable.xcstrings" \
        "${resources}/Localizable.xcstrings"
    cp "${PROJECT_ROOT}/macos/Resources/PrivacyInfo.xcprivacy" \
        "${resources}/PrivacyInfo.xcprivacy"
    python3 "$ICON_SCRIPT" \
        --iconset "${TMPDIR}/AppIcon.iconset" \
        --icns "${resources}/AppIcon.icns"
}

add_test_dylib() {
    local app="$1"
    local name="$2"
    local frameworks="${app}/Contents/Frameworks"

    mkdir -p "$frameworks"
    cat >"${TMPDIR}/framework.c" <<'EOF'
int mgb64_verifier_test_dylib(void) { return 0; }
EOF
    cc -dynamiclib "${TMPDIR}/framework.c" -o "${frameworks}/${name}"
}

copy_app() {
    local dst="$1"
    rm -rf "$dst"
    cp -R "$BASE_APP" "$dst"
}

assert_pass() {
    local app="$1"
    local log="$2"
    if ! "$VERIFY_SCRIPT" "$app" >"$log" 2>&1; then
        cat "$log"
        echo "FAIL: expected verifier success for $app" >&2
        exit 1
    fi
}

assert_fail_contains() {
    local app="$1"
    local expected="$2"
    local log="$3"

    if "$VERIFY_SCRIPT" "$app" >"$log" 2>&1; then
        cat "$log"
        echo "FAIL: expected verifier failure for $app" >&2
        exit 1
    fi

    if ! grep -Fq "$expected" "$log"; then
        cat "$log"
        echo "FAIL: expected verifier output to contain: $expected" >&2
        exit 1
    fi
}

echo "=== Building synthetic clean app ==="
make_clean_app "$BASE_APP"

echo "=== Asset-free verifier regression tests ==="
assert_pass "$BASE_APP" "${TMPDIR}/clean.log"
echo "  clean bundle: PASS"

SDL_APP="${TMPDIR}/BundledSDL.app"
copy_app "$SDL_APP"
add_test_dylib "$SDL_APP" "libSDL2-2.0.0.dylib"
assert_pass "$SDL_APP" "${TMPDIR}/bundled_sdl.log"
echo "  bundled SDL2 dylib allowlist: PASS"

BAD_APP="${TMPDIR}/ForbiddenResource.app"
copy_app "$BAD_APP"
touch "${BAD_APP}/Contents/Resources/accidental.z64"
assert_fail_contains "$BAD_APP" "Forbidden ROM/media-like resource" "${TMPDIR}/forbidden.log"
echo "  forbidden resource extension: PASS"

BAD_APP="${TMPDIR}/UnexpectedResource.app"
copy_app "$BAD_APP"
printf 'notes\n' >"${BAD_APP}/Contents/Resources/notes.txt"
assert_fail_contains "$BAD_APP" "Unexpected app resource" "${TMPDIR}/unexpected.log"
echo "  unexpected resource allowlist: PASS"

BAD_APP="${TMPDIR}/MissingIcon.app"
copy_app "$BAD_APP"
rm -f "${BAD_APP}/Contents/Resources/AppIcon.icns"
assert_fail_contains "$BAD_APP" "Declared app icon is missing" "${TMPDIR}/missing_icon.log"
echo "  missing declared icon: PASS"

BAD_APP="${TMPDIR}/ExecutableRomMagic.app"
copy_app "$BAD_APP"
printf '\100\022\067\200' >>"${BAD_APP}/Contents/MacOS/Clean"
assert_fail_contains "$BAD_APP" "n64 little-endian" "${TMPDIR}/executable_rom_magic.log"
echo "  embedded ROM magic in executable: PASS"

BAD_APP="${TMPDIR}/ResourceRomMagicZ64.app"
copy_app "$BAD_APP"
printf '\200\067\022\100' >>"${BAD_APP}/Contents/Resources/PrivacyInfo.xcprivacy"
assert_fail_contains "$BAD_APP" "Embedded N64 ROM bootstrap magic" "${TMPDIR}/rom_magic.log"
echo "  embedded .z64 ROM magic in resource: PASS"

BAD_APP="${TMPDIR}/ResourceRomMagicV64.app"
copy_app "$BAD_APP"
printf '\067\200\100\022' >>"${BAD_APP}/Contents/Resources/PrivacyInfo.xcprivacy"
assert_fail_contains "$BAD_APP" "v64 byte-swapped" "${TMPDIR}/resource_v64_rom_magic.log"
echo "  embedded .v64 ROM magic in resource: PASS"

BAD_APP="${TMPDIR}/UnexpectedFramework.app"
copy_app "$BAD_APP"
add_test_dylib "$BAD_APP" "libUnexpected.dylib"
assert_fail_contains "$BAD_APP" "Unexpected app framework/library payload" "${TMPDIR}/unexpected_framework.log"
echo "  unexpected framework allowlist: PASS"

BAD_APP="${TMPDIR}/FrameworkRomMagic.app"
copy_app "$BAD_APP"
add_test_dylib "$BAD_APP" "libSDL2-2.0.0.dylib"
printf '\200\067\022\100' >>"${BAD_APP}/Contents/Frameworks/libSDL2-2.0.0.dylib"
assert_fail_contains "$BAD_APP" "Embedded N64 ROM bootstrap magic found in app framework/library" "${TMPDIR}/framework_rom_magic.log"
echo "  embedded .z64 ROM magic in framework: PASS"

BAD_APP="${TMPDIR}/FrameworkRomMagicN64.app"
copy_app "$BAD_APP"
add_test_dylib "$BAD_APP" "libSDL2-2.0.0.dylib"
printf '\100\022\067\200' >>"${BAD_APP}/Contents/Frameworks/libSDL2-2.0.0.dylib"
assert_fail_contains "$BAD_APP" "n64 little-endian" "${TMPDIR}/framework_n64_rom_magic.log"
echo "  embedded .n64 ROM magic in framework: PASS"

echo "=== Asset-free verifier tests: PASS ==="
