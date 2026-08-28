#!/bin/bash
set -euo pipefail
#
# verify_asset_free.sh -- confirm a macOS binary/app is free of ROM-derived assets
#
# Purpose
# -------
# The N64 game port builds against stub/placeholder data so that the
# resulting binary never ships copyrighted ROM content (sprites, animations,
# Rareware logo bitmaps, etc.).  This script inspects a compiled Mach-O
# binary and fails the build if any contamination vector is detected.
#
# When to run
# -----------
#   - As a post-link CI step (e.g. in a GitHub Actions workflow).
#   - Locally before tagging a release.
#   - Any time the asset pipeline changes and you want a quick sanity check.
#
# Scope
# -----
# This verifier requires a BUILT binary/app-bundle argument (a Mach-O to
# inspect); run with no argument it prints usage and exits 1 by design -- it is
# not a source-tree scanner. The source-tree asset-free gate is
# scripts/ci/check_no_rom_data.sh (run inside scripts/ci/check_release_ready.sh),
# which is what gates a source-only checkout. Point this script at the built
# .app/binary (ideally freshly built from the tagged commit) as the
# complementary post-build check.
#
# Exit codes
#   0  All checks passed (WARNs are tolerated).
#   1  One or more checks FAILed -- binary is NOT asset-free.
#

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0

pass() {
    printf "  [PASS] %s\n" "$1"
    (( PASS_COUNT++ )) || true
}

fail() {
    printf "  [FAIL] %s\n" "$1"
    (( FAIL_COUNT++ )) || true
}

warn() {
    printf "  [WARN] %s\n" "$1"
    (( WARN_COUNT++ )) || true
}

scan_bootstrap_magic_file() {
    python3 - "$1" <<'PY'
import pathlib
import sys

signatures = [
    ("z64 big-endian", b"\x80\x37\x12\x40"),
    ("v64 byte-swapped", b"\x37\x80\x40\x12"),
    ("n64 little-endian", b"\x40\x12\x37\x80"),
]

try:
    data = pathlib.Path(sys.argv[1]).read_bytes()
except OSError:
    sys.exit(2)

for label, sig in signatures:
    if sig in data:
        print(f"{label} ({sig.hex(' ')})")
        sys.exit(0)

sys.exit(1)
PY
}

scan_bootstrap_magic_tree() {
    python3 - "$1" <<'PY'
import pathlib
import sys

signatures = [
    ("z64 big-endian", b"\x80\x37\x12\x40"),
    ("v64 byte-swapped", b"\x37\x80\x40\x12"),
    ("n64 little-endian", b"\x40\x12\x37\x80"),
]

root = pathlib.Path(sys.argv[1])
bad = []
for path in root.rglob("*"):
    if not path.is_file():
        continue
    try:
        data = path.read_bytes()
    except OSError:
        continue
    for label, sig in signatures:
        if sig in data:
            bad.append(f"{path.relative_to(root.parent.parent)}: {label} ({sig.hex(' ')})")
            break

if bad:
    print("\n".join(bad))
    sys.exit(1)

sys.exit(0)
PY
}

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <path-to-macos-binary-or-app-bundle>"
    echo ""
    echo "Verifies that the built binary/app contains no ROM-derived copyrighted"
    echo "content. Returns 0 on success, 1 if contamination is detected."
    exit 1
fi

INPUT_PATH="$1"
BINARY="$INPUT_PATH"
APP_BUNDLE_INPUT=false
APP_BUNDLE=""

if [[ -d "$BINARY" && "$BINARY" == *.app ]]; then
    APP_BUNDLE_INPUT=true
    APP_BUNDLE="$BINARY"
    INFO_PLIST="${APP_BUNDLE}/Contents/Info.plist"
    if [[ ! -f "${INFO_PLIST}" ]]; then
        echo "ERROR: app bundle is missing Info.plist: ${APP_BUNDLE}"
        exit 1
    fi
    EXECUTABLE_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "${INFO_PLIST}" 2>/dev/null || true)"
    if [[ -z "${EXECUTABLE_NAME}" ]]; then
        echo "ERROR: app bundle Info.plist is missing CFBundleExecutable: ${INFO_PLIST}"
        exit 1
    fi
    BINARY="${APP_BUNDLE}/Contents/MacOS/${EXECUTABLE_NAME}"
fi

if [[ ! -f "$BINARY" ]]; then
    echo "ERROR: file not found: $BINARY"
    exit 1
fi

echo "============================================================"
echo "  Asset-Free Verification"
echo "  Binary: $BINARY"
echo "============================================================"
echo ""

# ---------------------------------------------------------------------------
# 1. Rareware logo data
#
#    The symbol imgRAre_0x0020 will exist even when stubbed out, but a stub
#    is typically zero-filled and tiny (<= 64 bytes).  Real logo data is
#    several hundred bytes or more.
# ---------------------------------------------------------------------------

echo "--- Check 1: Rareware logo symbol (imgRAre_0x0020) ---"

# nm -m prints Mach-O symbol info; we look for the symbol and its size.
# On macOS, `nm -m -P` gives POSIX output: name type section size
RARE_LINE=$(nm -P "$BINARY" 2>/dev/null | grep -E '^_?imgRAre_0x0020 ' || true)

if [[ -z "$RARE_LINE" ]]; then
    pass "imgRAre_0x0020 symbol not present -- no logo data compiled in."
else
    # POSIX nm format: symbol_name type_letter value size
    # Size is the last hex field.
    RARE_SIZE_HEX=$(echo "$RARE_LINE" | awk '{print $NF}')
    RARE_SIZE=$((16#${RARE_SIZE_HEX})) 2>/dev/null || RARE_SIZE=0

    if (( RARE_SIZE > 64 )); then
        fail "imgRAre_0x0020 has $RARE_SIZE bytes of data (threshold: 64). Logo data is compiled in!"
    else
        pass "imgRAre_0x0020 present but only $RARE_SIZE bytes (stub/zero-fill, <= 64)."
    fi
fi

echo ""

# ---------------------------------------------------------------------------
# 2. Animation data
#
#    ANIM_DATA_idle is the most common animation blob.  A stub will have the
#    symbol at zero or trivial size; real animation data is hundreds of bytes.
# ---------------------------------------------------------------------------

echo "--- Check 2: Animation data symbol (ANIM_DATA_idle) ---"

ANIM_LINE=$(nm -P "$BINARY" 2>/dev/null | grep -E '^_?ANIM_DATA_idle ' || true)

if [[ -z "$ANIM_LINE" ]]; then
    pass "ANIM_DATA_idle symbol not present -- no animation data compiled in."
else
    ANIM_SIZE_HEX=$(echo "$ANIM_LINE" | awk '{print $NF}')
    ANIM_SIZE=$((16#${ANIM_SIZE_HEX})) 2>/dev/null || ANIM_SIZE=0

    if (( ANIM_SIZE > 64 )); then
        fail "ANIM_DATA_idle has $ANIM_SIZE bytes of data (threshold: 64). Animation data is compiled in!"
    else
        pass "ANIM_DATA_idle present but only $ANIM_SIZE bytes (stub/zero-fill, <= 64)."
    fi
fi

echo ""

# ---------------------------------------------------------------------------
# 3. Known ROM byte signatures
#
#    N64 ROM headers contain identifiable ASCII strings.  If any of these
#    appear in the final binary, ROM content has leaked in.
# ---------------------------------------------------------------------------

echo "--- Check 3: embedded-ROM signature (bootstrap magic) ---"

# We detect an *embedded ROM* by the N64 bootstrap magic bytes appearing
# contiguously in the binary's data — that only happens if a real ROM image
# leaked in. We deliberately do NOT flag the ASCII name "GOLDENEYE": it appears
# legitimately in our own source (the ROM auto-detector matches the cartridge
# name, enum identifiers reference it, etc.), so a name-string match is not
# evidence of leaked ROM data. Bulk leakage is caught by Check 4 (data segment
# size) and Checks 1/2/5 (asset symbols); this check catches a raw ROM blob.

# N64 bootstrap magic (PI register init) appears at the start of ROM images in
# all common byte orders. In our source these bytes only exist as separate C
# constants, never contiguously, so a contiguous match means an actual ROM image
# or byte-swapped ROM dump is embedded.
FOUND_SIG=0
if SIG_MATCH="$(scan_bootstrap_magic_file "$BINARY" 2>/dev/null)"; then
    fail "N64 bootstrap magic (${SIG_MATCH}) found contiguously — a ROM image is embedded!"
    FOUND_SIG=1
fi

if (( FOUND_SIG == 0 )); then
    pass "No embedded-ROM bootstrap magic detected."
fi

echo ""

# ---------------------------------------------------------------------------
# 4. Data segment size check
#
#    A clean port binary (code + small stubs) should have a data segment
#    well under 200 KB.  If compiled-in assets push it past 500 KB,
#    something is wrong.
# ---------------------------------------------------------------------------

echo "--- Check 4: Data segment size ---"

# `size` on macOS prints:  __TEXT  __DATA  __OBJC  others  ...
# We want the __DATA column.  With -m (Mach-O format) we can parse more
# reliably, but the default BSD output is simpler:
#   __TEXT  __DATA  __OBJC  others  hex     decimal
# Column 2 is __DATA.

DATA_SIZE=$(size "$BINARY" 2>/dev/null | tail -1 | awk '{print $2}')

if [[ -z "$DATA_SIZE" || "$DATA_SIZE" == "0" ]]; then
    warn "Could not determine data segment size (non-standard binary format?)."
else
    DATA_KB=$(( DATA_SIZE / 1024 ))
    # `size` counts __bss in the __DATA column for a whole Mach-O image. This
    # engine statically reserves the N64's 8 MB RDRAM (+ framebuffers/audio) as
    # BSS -- ~10 MB of __DATA that carries no bytes on disk; the GL app adds
    # ImGui/font data on top (~10-11 MB total). A compiled-in ROM would appear
    # as ~12 MB of REAL initialized __data, pushing the image past ~22 MB. So
    # app bundles AND standalone executables/dylibs use a 16 MB BSS-aware
    # threshold; the tight 500 KB threshold applies only to static libraries
    # (.a), whose per-object __DATA carries no such reservation.
    # Classify the binary so the right data-segment threshold applies. A ROM
    # would show up as ~12 MB of real initialized data on every platform; what
    # differs is how `size` measures the CLEAN baseline.
    IS_MACHO_IMAGE=false   # macOS `size` folds __bss into __DATA (~10 MB base)
    IS_ELF_IMAGE=false     # Linux `size` DATA is REAL data (bss is a separate col)
    if file "$BINARY" 2>/dev/null | grep -qiE "Mach-O.*(executable|shared library|dylib)"; then
        IS_MACHO_IMAGE=true
    elif file "$BINARY" 2>/dev/null | grep -qiE "ELF.*(executable|shared object)"; then
        IS_ELF_IMAGE=true
    fi
    if [[ "${IS_ELF_IMAGE}" == true ]]; then
        # Linux ELF program: the 8 MB RDRAM reservation lives in BSS (a separate
        # `size` column), so the DATA column is real initialized data/rodata. A
        # clean port is well under 1 MB (WGSL/combiner string tables etc.); a
        # compiled-in ROM adds ~12 MB. A 4 MB FAIL catches the ROM with headroom
        # for legitimate code growth. (Previously an ELF executable fell through to
        # the 500 KB static-library branch below — a misclassification, per this
        # block's own "standalone executables use the BSS-aware threshold" intent —
        # which false-positived once legit rodata crossed 500 KB. Checks 1/2/3/5
        # remain the primary ROM detectors; this is only the coarse size backstop.)
        if (( DATA_SIZE > 4194304 )); then
            fail "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Threshold: 4 MB (ELF program). Assets may be compiled in."
        elif (( DATA_SIZE > 2097152 )); then
            warn "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Above 2 MB -- review for embedded assets."
        else
            pass "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Within limits (ELF program)."
        fi
    elif [[ "${APP_BUNDLE_INPUT}" == true || "${IS_MACHO_IMAGE}" == true ]]; then
        if (( DATA_SIZE > 16777216 )); then
            fail "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Threshold: 16 MB. Assets may be compiled in."
        elif (( DATA_SIZE > 12582912 )); then
            warn "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Above 12 MB -- review for embedded assets."
        else
            pass "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Within limits (BSS-aware)."
        fi
    elif (( DATA_SIZE > 512000 )); then
        fail "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Threshold: 500 KB. Assets may be compiled in."
    elif (( DATA_SIZE > 204800 )); then
        warn "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Above 200 KB -- review for embedded assets."
    else
        pass "Data segment is ${DATA_KB} KB (${DATA_SIZE} bytes). Well within limits."
    fi
fi

echo ""

# ---------------------------------------------------------------------------
# 5. Exhaustive symbol scan
#
#    Look for ALL symbols matching the asset-data patterns and flag any
#    that carry substantial data (> 64 bytes).
# ---------------------------------------------------------------------------

echo "--- Check 5: Exhaustive asset symbol scan ---"

# Collect all matching symbols with POSIX nm output.
#
# NOTE: globalDL_* are intentionally NOT scanned. They are display-list CODE
# (RDP/RSP Gfx command arrays in the committed GlobalImageTable.c), not ROM
# media payload — they contain no pixels/audio and are part of the decompiled
# source. ANIM_DATA_* (animation frames) and imgRAre_* (Rareware logo image)
# ARE scanned: those are bulk media that must be loaded from the user's ROM at
# runtime, so they must NOT carry real data in the binary.
ASSET_SYMBOLS=$(nm -P "$BINARY" 2>/dev/null \
    | grep -E '^_?(ANIM_DATA_|imgRAre_)' || true)

if [[ -z "$ASSET_SYMBOLS" ]]; then
    pass "No ANIM_DATA_* or imgRAre_* media symbols found at all."
else
    LARGE_COUNT=0
    TOTAL_COUNT=0
    LARGE_DETAILS=""

    while IFS= read -r LINE; do
        (( TOTAL_COUNT++ )) || true
        SYM_NAME=$(echo "$LINE" | awk '{print $1}')
        SYM_SIZE_HEX=$(echo "$LINE" | awk '{print $NF}')
        SYM_SIZE=$((16#${SYM_SIZE_HEX})) 2>/dev/null || SYM_SIZE=0

        if (( SYM_SIZE > 64 )); then
            (( LARGE_COUNT++ )) || true
            LARGE_DETAILS="${LARGE_DETAILS}    ${SYM_NAME} (${SYM_SIZE} bytes)\n"
        fi
    done <<< "$ASSET_SYMBOLS"

    if (( LARGE_COUNT > 0 )); then
        warn "${LARGE_COUNT} of ${TOTAL_COUNT} asset symbol(s) exceed 64 bytes:"
        printf "%b" "$LARGE_DETAILS"
    else
        pass "All ${TOTAL_COUNT} asset symbol(s) are <= 64 bytes (stubs)."
    fi
fi

echo ""

# ---------------------------------------------------------------------------
# 6. App bundle resource hygiene
#
#    A clean app bundle may contain project-owned UI resources, such as the
#    generated AppIcon.icns and Apple metadata catalogs. It must not carry ROMs,
#    extracted game assets, screenshots, audio dumps, or other opaque payloads
#    outside the executable.
# ---------------------------------------------------------------------------

if [[ "${APP_BUNDLE_INPUT}" == true ]]; then
    echo "--- Check 6: App bundle resources ---"

    RESOURCE_FAIL=0
    RESOURCE_DIR="${APP_BUNDLE}/Contents/Resources"

    ICON_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIconFile' "${INFO_PLIST}" 2>/dev/null || true)"
    if [[ -z "${ICON_NAME}" ]]; then
        fail "App bundle Info.plist is missing CFBundleIconFile."
        RESOURCE_FAIL=1
    else
        case "${ICON_NAME}" in
            *.icns) ICON_FILE="${RESOURCE_DIR}/${ICON_NAME}" ;;
            *)      ICON_FILE="${RESOURCE_DIR}/${ICON_NAME}.icns" ;;
        esac

        if [[ -s "${ICON_FILE}" ]]; then
            pass "Declared app icon exists: ${ICON_FILE#${APP_BUNDLE}/}"
        else
            fail "Declared app icon is missing or empty: ${ICON_FILE#${APP_BUNDLE}/}"
            RESOURCE_FAIL=1
        fi
    fi

    if [[ ! -d "${RESOURCE_DIR}" ]]; then
        fail "App bundle is missing Contents/Resources."
        RESOURCE_FAIL=1
    else
        while IFS= read -r RESOURCE; do
            REL="${RESOURCE#${APP_BUNDLE}/}"
            BASE="$(basename "${RESOURCE}")"

            case "${REL}" in
                Contents/Resources/AppIcon.icns|\
                Contents/Resources/Localizable.xcstrings|\
                Contents/Resources/PrivacyInfo.xcprivacy|\
                Contents/Resources/gamecontrollerdb.txt)
                    # SDL_GameControllerDB: plain-text community pad mappings
                    # (zlib; THIRD_PARTY.md + lib/sdl_gamecontrollerdb/LICENSE.txt).
                    # No ROM/asset content -- deliberately shipped so exotic and
                    # handheld controllers map out of the box.
                    ;;
                *)
                    fail "Unexpected app resource in asset-free bundle: ${REL}"
                    RESOURCE_FAIL=1
                    ;;
            esac

            case "${BASE}" in
                *.z64|*.Z64|*.n64|*.N64|*.v64|*.V64|baserom*|\
                *.bin|*.BIN|*.cdata|*.CDATA|*.ctl|*.CTL|*.tbl|*.TBL|*.sbk|*.SBK|*.seq|*.SEQ|\
                *.aifc|*.AIFC|*.aiff|*.AIFF|*.bmp|*.BMP|*.png|*.PNG|*.jpg|*.JPG|*.jpeg|*.JPEG|\
                *.gif|*.GIF|*.webp|*.WEBP|*.ppm|*.PPM|*.raw|*.RAW|*.wav|*.WAV|*.mp3|*.MP3|\
                *.ogg|*.OGG|*.flac|*.FLAC)
                    fail "Forbidden ROM/media-like resource in app bundle: ${REL}"
                    RESOURCE_FAIL=1
                    ;;
            esac
        done < <(find "${RESOURCE_DIR}" -type f -print)

        if RESOURCE_MAGIC_MATCHES="$(scan_bootstrap_magic_tree "${RESOURCE_DIR}" 2>/dev/null)"
        then
            :
        else
            printf '%s\n' "${RESOURCE_MAGIC_MATCHES}"
            fail "Embedded N64 ROM bootstrap magic found in app resource(s)."
            RESOURCE_FAIL=1
        fi
    fi

    if (( RESOURCE_FAIL == 0 )); then
        pass "No unexpected ROM/media resource payloads detected."
    fi

    echo ""

    echo "--- Check 7: App bundle frameworks ---"

    FRAMEWORK_FAIL=0
    FRAMEWORKS_DIR="${APP_BUNDLE}/Contents/Frameworks"

    if [[ ! -d "${FRAMEWORKS_DIR}" ]]; then
        pass "No embedded Frameworks directory present."
    else
        while IFS= read -r FRAMEWORK_FILE; do
            REL="${FRAMEWORK_FILE#${APP_BUNDLE}/}"
            BASE="$(basename "${FRAMEWORK_FILE}")"

            case "${REL}" in
                Contents/Frameworks/libSDL2-2.0.0.dylib|\
                Contents/Frameworks/libSDL2.dylib)
                    ;;
                *)
                    fail "Unexpected app framework/library payload in asset-free bundle: ${REL}"
                    FRAMEWORK_FAIL=1
                    ;;
            esac

            case "${BASE}" in
                *.z64|*.Z64|*.n64|*.N64|*.v64|*.V64|baserom*|\
                *.bin|*.BIN|*.cdata|*.CDATA|*.ctl|*.CTL|*.tbl|*.TBL|*.sbk|*.SBK|*.seq|*.SEQ|\
                *.aifc|*.AIFC|*.aiff|*.AIFF|*.bmp|*.BMP|*.png|*.PNG|*.jpg|*.JPG|*.jpeg|*.JPEG|\
                *.gif|*.GIF|*.webp|*.WEBP|*.ppm|*.PPM|*.raw|*.RAW|*.wav|*.WAV|*.mp3|*.MP3|\
                *.ogg|*.OGG|*.flac|*.FLAC)
                    fail "Forbidden ROM/media-like framework payload in app bundle: ${REL}"
                    FRAMEWORK_FAIL=1
                    ;;
            esac

            if FRAMEWORK_SIG_MATCH="$(scan_bootstrap_magic_file "${FRAMEWORK_FILE}" 2>/dev/null)"; then
                fail "Embedded N64 ROM bootstrap magic found in app framework/library: ${REL} (${FRAMEWORK_SIG_MATCH})"
                FRAMEWORK_FAIL=1
            fi

            ASSET_FRAMEWORK_SYMBOLS=$(nm -P "${FRAMEWORK_FILE}" 2>/dev/null \
                | grep -E '^_?(ANIM_DATA_|imgRAre_)' || true)
            if [[ -n "${ASSET_FRAMEWORK_SYMBOLS}" ]]; then
                fail "Asset-data symbol found in app framework/library: ${REL}"
                FRAMEWORK_FAIL=1
            fi
        done < <(find "${FRAMEWORKS_DIR}" -type f -print)

        if (( FRAMEWORK_FAIL == 0 )); then
            pass "No unexpected ROM/media framework payloads detected."
        fi
    fi

    echo ""
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo "============================================================"
echo "  Summary"
echo "    PASS: $PASS_COUNT"
echo "    FAIL: $FAIL_COUNT"
echo "    WARN: $WARN_COUNT"
echo "============================================================"

if (( FAIL_COUNT > 0 )); then
    echo ""
    if [[ "${APP_BUNDLE_INPUT}" == true ]]; then
        echo "RESULT: FAILED -- app bundle contains ROM-derived content."
    else
        echo "RESULT: FAILED -- binary contains ROM-derived content."
    fi
    echo "        Do NOT distribute this build."
    exit 1
else
    echo ""
    if (( WARN_COUNT > 0 )); then
        echo "RESULT: PASSED (with ${WARN_COUNT} warning(s) -- review recommended)."
    else
        if [[ "${APP_BUNDLE_INPUT}" == true ]]; then
            echo "RESULT: PASSED -- app bundle is asset-free."
        else
            echo "RESULT: PASSED -- binary is asset-free."
        fi
    fi
    exit 0
fi
