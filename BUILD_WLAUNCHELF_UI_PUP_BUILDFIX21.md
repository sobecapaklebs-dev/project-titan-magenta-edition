# Project Titan Magenta Edition RC14 — BuildFix21

Base: `Project-Titan-Magenta-Edition-0.3.90-rc14-library-updater-package-stability-buildfix20-source.tar(1).gz`.

This build merges the parallel Titan-native wLaunchELF port into the complete BuildFix20 Library/PUP/Package-Installer baseline and applies the first UI-fidelity pass requested from hardware photos.

## UI fixes

- Library stays the permanent final Home tile; its selected title is explicitly anchored to the right of the tile.
- Removed the Library top-right `Offline` label/icon that collided with the system clock.
- Folder destination/editor/content screens now own the content plane: Home tiles and the Home header are not rendered behind them.
- Removed the almost-opaque black folder fullscreen backdrop; the animated Titan wave remains visible.
- Folder option buttons, name/count fields and content cards no longer use black card fills; focus is expressed with Magenta accents/outlines.
- Reduced the global idle help bar from 34 logical pixels to 24 and reduced footer text scale.

## wLaunchELF merge

- Added `apps/wlaunchelf` Titan-native `.tmod` port foundation.
- Titan App API v1 remains append-only and gains kernel-owned absolute storage operations plus validated ELF handoff.
- Current app milestone browses device roots/directories, copies regular files cooperatively, removes files/empty folders and launches `.elf` through Titan stage2.
- Direct FileXio/IRX/gsKit/ExecPS2 ownership remains prohibited inside the app.

## First PUP

BuildFix21 advances `TITAN_SYSTEM_UPDATE_CURRENT_BUILD` to 21. From the repository root, after a successful PS2DEV MIPS build:

```text
make -C kernel pup
```

The target builds/verifies:

```text
Project-Titan-Magenta-Edition-0.3.90-buildfix21-wlaunchelf-ui.pup
```

The PUP contains:

```text
kernel.elf
TITAN/packages/WLAUNCHELF.pkg
```

BuildFix20 must already be running once so its TPUP v1 updater can consume this BuildFix21 package. After update/reboot, open Package Installer and install `WLAUNCHELF.pkg`.

## Validation performed here

- `make -C apps/wlaunchelf host-test`: PASS
- system-update multi-file publication + rollback test: PASS
- wLaunchELF native-port contract: PASS
- BuildFix21 UI/wLaunchELF merge contract: PASS
- Existing host suite passed through the renderer/audio/UTF-8/source-hardening/Library contracts in repeated runs; the monolithic script exceeds this environment's execution window before completion, so the remaining updater/wLaunchELF tail was executed separately and passed.

No PS2DEV MIPS compiler is installed in this execution environment, so the actual `kernel.elf`, `.tmod`, `.pkg` and final `.pup` binary must be produced on the user's PS2DEV host.
