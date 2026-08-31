#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SOURCE="$ROOT/modules/titan-ul-engine"
OUTPUT="$ROOT/build/modules/titan-ul-engine"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
SOURCE_COMMIT=${TITAN_SOURCE_COMMIT:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)}
if [[ "$SOURCE_COMMIT" =~ ^[0-9a-fA-F]{7,}$ ]]; then
    OPL_GIT_HASH=${SOURCE_COMMIT:0:7}
else
    OPL_GIT_HASH=archive
fi

[[ -n "${PS2DEV:-}" && -n "${PS2SDK:-}" ]] || {
    echo "PS2DEV and PS2SDK must be exported." >&2
    exit 1
}

gskit_root_is_usable() {
    local root=${1:-}
    [[ -n "$root" ]] || return 1
    [[ -f "$root/include/gsKit.h" ]] || return 1
    [[ -f "$root/lib/libgskit.a" ]] || return 1
    [[ -f "$root/lib/libdmakit.a" ]] || return 1
}

resolve_gskit() {
    local candidate header

    # Respect a valid explicit GSKIT first.  If a stale shell value exists,
    # recover automatically instead of allowing OPL to degrade into -I/include.
    if gskit_root_is_usable "${GSKIT:-}"; then
        GSKIT=$(cd "$GSKIT" && pwd -P)
        export GSKIT
        return 0
    fi
    if [[ -n "${GSKIT:-}" ]]; then
        echo "WARNING: ignoring unusable GSKIT='$GSKIT'; auto-detecting gsKit." >&2
    fi

    # Standard ps2dev layouts used by the project and by common local builds.
    for candidate in \
        "$PS2DEV/gsKit" \
        "$PS2SDK/../gsKit" \
        "$(dirname "$PS2DEV")/gsKit"; do
        if gskit_root_is_usable "$candidate"; then
            GSKIT=$(cd "$candidate" && pwd -P)
            export GSKIT
            return 0
        fi
    done

    # Last-resort bounded search inside PS2DEV.  This keeps extracted release
    # trees self-configuring without scanning the user's entire home directory.
    header=$(find "$PS2DEV" -maxdepth 5 -type f -path '*/include/gsKit.h' -print -quit 2>/dev/null || true)
    if [[ -n "$header" ]]; then
        candidate=${header%/include/gsKit.h}
        if gskit_root_is_usable "$candidate"; then
            GSKIT=$(cd "$candidate" && pwd -P)
            export GSKIT
            return 0
        fi
    fi

    cat >&2 <<EOF
Titan UL Engine requires a built gsKit tree.
Could not auto-detect one from PS2DEV='$PS2DEV' or PS2SDK='$PS2SDK'.
Expected gsKit.h plus libgskit.a and libdmakit.a under the same gsKit root.
You may override detection with: export GSKIT=/path/to/gsKit
EOF
    exit 1
}

resolve_gskit

if [[ ${1:-} == --check-env ]]; then
    printf 'PS2DEV=%s\nPS2SDK=%s\nGSKIT=%s\n' "$PS2DEV" "$PS2SDK" "$GSKIT"
    exit 0
fi

sandbox=$(mktemp -d "${TMPDIR:-/tmp}/titan-ul-engine.XXXXXX")
work="$sandbox/modules/titan-ul-engine"
overlay="$sandbox/modules/titan-engine-overlay"
mkdir -p "$work" "$overlay"
cleanup() { rm -rf "$sandbox"; }
trap cleanup EXIT

# Build a disposable copy while preserving the repository's modules/* sibling
# geometry. ee_core references ../../titan-engine-overlay, so flattening the UL
# tree directly into /tmp makes that dependency escape the sandbox.
tar -C "$SOURCE" \
    --exclude='.git' --exclude='*.elf' --exclude='OPNPS2LD*' \
    --exclude='obj*' --exclude='build' \
    -cf - . | tar -C "$work" -xf -
tar -C "$ROOT/modules/titan-engine-overlay" -cf - . | tar -C "$overlay" -xf -

export LC_ALL=C TZ=UTC

# TITAN_UL_PINNED_IMPORTS_V4
# PADEMU in Project Titan contains post-upstream controller work. Preserve the
# validated prebuilt IRXs instead of rebuilding that unrelated subsystem while
# rebuilding Titan UL for a GSM-only EE change.
pademu_keep="$sandbox/.titan-pademu-frozen"
mkdir -p "$pademu_keep"
for f in bt_pademu.irx usb_pademu.irx; do
    [[ -s "$work/modules/pademu/$f" ]] || { echo "Titan UL frozen PADEMU missing: $f" >&2; exit 1; }
    cp -f "$work/modules/pademu/$f" "$pademu_keep/$f"
done
bt_before=$(sha256sum "$pademu_keep/bt_pademu.irx" | awk '{print $1}')
usb_before=$(sha256sum "$pademu_keep/usb_pademu.irx" | awk '{print $1}')

make -C "$work" clean REVISION=0 GIT_HASH="$OPL_GIT_HASH" DIRTY= GIT_TAG=

cp -f "$pademu_keep/bt_pademu.irx" "$work/modules/pademu/bt_pademu.irx"
cp -f "$pademu_keep/usb_pademu.irx" "$work/modules/pademu/usb_pademu.irx"
# Ensure the restored binary targets are newer than the pademu directory.
touch "$work/modules/pademu/bt_pademu.irx" "$work/modules/pademu/usb_pademu.irx"

make --output-sync=recurse -C "$work" -j"$JOBS"     -o modules/pademu/bt_pademu.irx     -o modules/pademu/usb_pademu.irx     REVISION=0 GIT_HASH="$OPL_GIT_HASH" DIRTY= GIT_TAG=

bt_after=$(sha256sum "$work/modules/pademu/bt_pademu.irx" | awk '{print $1}')
usb_after=$(sha256sum "$work/modules/pademu/usb_pademu.irx" | awk '{print $1}')
[[ "$bt_before" == "$bt_after" && "$usb_before" == "$usb_after" ]] || {
    echo "Titan UL frozen PADEMU changed during build." >&2
    exit 1
}
echo "TITAN_UL_PADEMU=FROZEN_PASS"
echo "TITAN_UL_PADEMU_BT_SHA256=$bt_after"
echo "TITAN_UL_PADEMU_USB_SHA256=$usb_after"

mkdir -p "$OUTPUT"
for artifact in OPNPS2LD.ELF opl_stripped.elf; do
    [[ -f "$work/$artifact" ]] || { echo "UL artifact missing: $artifact" >&2; exit 1; }
    cp -f "$work/$artifact" "$OUTPUT/$artifact"
done

{
    echo "component=Titan UL Engine"
    echo "source_commit=$SOURCE_COMMIT"
    echo "gskit=$GSKIT"
    echo "opl_upstream=$(awk -F': ' '/Upstream commit/{print $2}' "$SOURCE/UPSTREAM.md" 2>/dev/null || true)"
    echo "built_at=$(date --iso-8601=seconds)"
    "${PS2DEV}/ee/bin/mips64r5900el-ps2-elf-gcc" --version | head -n 1
    (cd "$OUTPUT" && sha256sum OPNPS2LD.ELF opl_stripped.elf)
} > "$OUTPUT/BUILD_MANIFEST.txt"

echo "Titan UL Engine artifacts: $OUTPUT"
