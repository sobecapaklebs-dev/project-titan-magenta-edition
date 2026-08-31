#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/titan-installer-test.XXXXXX")
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

package="$work/package"
target="$work/target"
mkdir -p "$package/TITAN/audio" "$package/neutrino" "$target/TITAN" "$target/neutrino"

printf 'new-kernel\n' > "$package/kernel.elf"
printf 'new-boot\n' > "$package/titan.elf"
printf 'new-ul\n' > "$package/TITAN/titan_ul_engine.elf"
printf 'new-ul\n' > "$package/TITAN/titan_ul_loader.elf"
printf 'new-neutrino\n' > "$package/neutrino/neutrino.elf"
printf 'sound\n' > "$package/TITAN/audio/test.wav"

printf 'old-kernel\n' > "$target/kernel.elf"
printf 'old-legacy-kernel\n' > "$target/TITAN/kernel.elf"
printf 'old-legacy-ul\n' > "$target/TITAN/titan_ul_loader.elf"

bash "$ROOT/scripts/install_to_usb.sh" --target "$target" --package "$package" >/dev/null

grep -qx 'new-kernel' "$target/kernel.elf"
grep -qx 'new-ul' "$target/TITAN/titan_ul_engine.elf"
grep -qx 'new-ul' "$target/TITAN/titan_ul_loader.elf"
grep -qx 'new-neutrino' "$target/neutrino/neutrino.elf"
[[ -f "$target/TITAN/INSTALL_MANIFEST.txt" ]]
[[ -f "$target/TITAN/INSTALL_SHA256.txt" ]]
compgen -G "$target/TITAN/kernel.elf.legacy-disabled-*" >/dev/null
if compgen -G "$target/TITAN/titan_ul_loader.elf.legacy-disabled-*" >/dev/null; then
    echo "ERROR: validated UL loader alias was legacy-disabled" >&2
    exit 1
fi

# Simulate an interrupted transaction that had already replaced kernel.elf.
mkdir -p "$target/TITAN/.install-transactions"
printf 'pre-crash-kernel\n' > "$target/kernel.elf.titan-backup-crash"
printf 'interrupted-new-kernel\n' > "$target/kernel.elf"
printf 'FILE|kernel.elf|%s|1\n' \
    "$target/kernel.elf.titan-backup-crash" \
    > "$target/TITAN/.install-transactions/crash.pending"

bash "$ROOT/scripts/install_to_usb.sh" --target "$target" --package "$package" >/dev/null
grep -qx 'new-kernel' "$target/kernel.elf"
# The recovered pre-crash file becomes the backup of the subsequent valid install.
grep -lqx 'pre-crash-kernel' "$target"/kernel.elf.titan-backup-* >/dev/null
[[ ! -e "$target/TITAN/.install-transactions/crash.pending" ]]

printf 'installer-transaction: OK\n'
