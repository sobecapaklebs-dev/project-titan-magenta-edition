# Project Titan: Magenta Edition — One-shot Installer POC v1

This implementation adds a one-shot installer mode to the Build 36 kernel. The same
ELF enters installer mode when its executable name contains `titan-installer` or
when `--installer` is present. Normal `kernel.elf` behavior is unchanged.

## Implemented flow

1. PS2 model detection (ROMVER + read-only MechaCon/NVRAM Model ID query).
2. First interactive controller screen with renderer-native illustration selected
   from Fat / 70xx-family Slim / 90xx-family Slim.
3. Language selection.
4. Welcome/hardware identification.
5. Read-only preflight before consent.
6. Semi-autonomous vs autonomous selection. Autonomous is hidden/disabled until a
   validated exploit payload profile is supplied; no fake exploit is written.
7. Memory Card selection and existing DEV1 `BOOT.ELF` analysis.
8. Destination summary.
9. Canonical VlombisCore Technology terms.
10. Final consent.
11. Manifest-driven staged copy with CRC32 revalidation.
12. Existing `mcX:/BOOT/BOOT.ELF` is renamed to `BOOT.TITAN.BAK` before the Titan
    bootloader is published; a failure attempts restoration.
13. `mass:/TITAN/INSTALL.COMPLETE` commit marker.
14. `TITAN_INSTALL/INSTALLER.CONSUMED` one-shot marker and best-effort deletion of
    the running installer ELF when it is under `mass:/TITAN_INSTALL/`.
15. Completion screen; X directly starts the freshly installed Titan bootloader.

## Build on the PS2DEV machine

From the project root:

```sh
make -C bootloader clean
make -C bootloader
make -C bootloader deploy
make -C kernel clean
make -C kernel installer
make -C kernel deploy
```

The installer target writes `titan-installer.elf` at the project root.

Build the normal runtime distribution using the existing project workflow, then
create installer media:

```sh
python3 tools/build_titan_installer_media.py \
  --output ./build/titan-installer-usb
```

Copy the **contents** of `build/titan-installer-usb/` to a FAT32 USB device and
launch:

```text
mass:/TITAN_INSTALL/titan-installer.elf
```

## First hardware test recommended

Use **semi-autonomous** mode first. It is the path intended for a Matrix/Modbo DEV1
setup: the Titan bootloader is installed at `mcX:/BOOT/BOOT.ELF`, while any existing
file at that path is retained as `mcX:/BOOT/BOOT.TITAN.BAK`.

Do not test Autonomous until a validated exploit payload has been supplied. The POC
intentionally refuses to pretend that an exploit exists.

## PS2 model identification

The installer first tries a read-only MechaCon version query and reads the model ID
from NVRAM word `0xE4` on older MechaCon revisions or `0xF8` on v4+. Known IDs are
mapped to SCPH model names. Unknown IDs retain the raw model ID and still select the
safest family illustration from MechaCon generation/range; the installer never
invents an exact SCPH string.
