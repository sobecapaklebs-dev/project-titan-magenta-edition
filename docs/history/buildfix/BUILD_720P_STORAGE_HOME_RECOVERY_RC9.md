# Project Titan Magenta Edition 0.3.90-rc9

## Versions

- Kernel: `0.3.90-rc9`
- Titan UI: `0.9.29`
- Bootloader metadata: `0.1.9-rc9`

## Scope

This release repairs the hardware regressions reported after RC8 without
changing approved visual assets or undoing the native 720p double-buffer
renderer.

- 720p changes only the GS CRTC display window to the hardware-centred
  coordinates used by the integrated Titan UL/GSM timing.
- The USB bootstrap always attempts every required IRX and then trusts the
  actual mounted-device probe.
- Transactional `mass:/TITAN` I/O transparently reaches `mass0:/TITAN` when
  that is the only first-device alias exposed by the loaded USB stack.
- Profiles, VMC, DEX, cache, folders, shell layout and package publication all
  share that corrected storage boundary.
- Generic discovered ELFs remain in Applications; direct Home tiles are
  limited to installed apps under `TITAN/apps`, so generic ELFs no longer hide
  the game catalog.

## Validation boundary

The complete host suite, ASan/UBSan coverage and source/release contracts pass.
This environment has not compiled the RC9 MIPS binary and has not tested RC9 in
PCSX2 or on a physical PS2. Hardware confirmation is still required.

## Build

```sh
make -C kernel clean
make -C kernel clean-generated
find kernel -type f -print0 | xargs -0 touch
make -C kernel system-assets
make -C kernel -j4
make -C kernel deploy
```
