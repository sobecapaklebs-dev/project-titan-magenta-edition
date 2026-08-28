#!/usr/bin/env bash
#
# test_rom_reject.sh — AUDIT-0005 regression guard: the engine ROM loader
# (platformInitRom, src/platform/rom_io.c) must reject a wrong 12 MB file
# BEFORE any SDL/audio/renderer/file-table-patch work, so a bad --rom exits
# cleanly instead of patching US GoldenEye offsets onto garbage and faulting in
# resource decompression at frame 0.
#
# ROM-free: every fixture is a synthetic sparse 12 MB file with a crafted
# 64-byte header — no copyrighted ROM bytes. The ACCEPT case cannot be tested
# here (it needs a real ROM body), so this pins the REJECT gates: wrong magic
# and wrong internal title. Each must exit nonzero, print "Refusing to boot",
# and never reach SDL init.
#
# Skips cleanly (exit 0) if the native binary is not built, like the other
# binary-dependent lanes.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"

BIN="${GE007_BIN:-$ROOT/build-webgpu/ge007}"
[ -x "$BIN" ] || BIN="$ROOT/build/ge007"
if [ ! -x "$BIN" ]; then
    echo "rom-reject: SKIP — no native binary at build-webgpu/ge007 or build/ge007"
    exit 0
fi

SIZE=12582912   # GE007_ROM_SIZE_BYTES (12 MB); must match so the size gate passes
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Craft a 64-byte header on a sparse 12 MB file. $1=path, $2..=byte-writes as
# "offset:hexbytes" (hex pairs). truncate makes the body sparse (cheap zeros).
mkfile() {
    local path="$1"; shift
    truncate -s "$SIZE" "$path"
    local spec off hex
    for spec in "$@"; do
        off="${spec%%:*}"; hex="${spec#*:}"
        printf '%b' "$(printf '\\x%s' $(echo "$hex" | fold -w2))" \
            | dd of="$path" bs=1 seek="$off" conv=notrunc status=none
    done
}

# Zero-filled: header magic 00 00 -> rejected at the N64-magic gate.
ZERO="$TMP/zero.z64"; mkfile "$ZERO"
# Valid N64 magic (80 37 12 40) but a non-GoldenEye title at 0x20 -> rejected at
# the title gate. The hex below is the ASCII title "NOTAREAGAME" + padding X's
# (any non-"GOLDENEYE" title exercises the gate; the exact bytes don't matter).
WRONG="$TMP/wrongtitle.z64"
mkfile "$WRONG" "0:80371240" "32:4e4f544152454147414d4558585858585858"

fail=0
check_reject() {  # $1=label $2=path
    local label="$1" path="$2" out rc
    out="$( SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 \
            timeout 30 "$BIN" --rom "$path" --level dam --deterministic --no-ui \
            --screenshot-frame 2 --screenshot-exit 2>&1 )" && rc=0 || rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "rom-reject: FAIL $label — accepted a bad ROM (exit 0)" >&2; fail=1; return
    fi
    if ! grep -q "Refusing to boot" <<<"$out"; then
        echo "rom-reject: FAIL $label — no 'Refusing to boot' diagnostic (exit $rc)" >&2; fail=1; return
    fi
    if grep -qiE "\[SDL\]|SDL_Init|propDefs:|Auto-screenshot" <<<"$out"; then
        echo "rom-reject: FAIL $label — reached SDL/setup before rejecting" >&2; fail=1; return
    fi
    echo "rom-reject: PASS $label (exit $rc, rejected before SDL)"
}

check_reject "zero-filled (bad magic)" "$ZERO"
check_reject "valid-magic wrong-title" "$WRONG"

if [ "$fail" -ne 0 ]; then
    echo "rom-reject: FAIL" >&2
    exit 1
fi
echo "rom-reject: PASS — bad ROMs rejected before SDL/audio/patch."
exit 0
