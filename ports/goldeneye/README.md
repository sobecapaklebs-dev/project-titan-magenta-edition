# GoldenEye / TitanEYE port workspace

This directory contains the exact pinned MGB64 source and the Project Titan
platform frontier. It does **not** contain a ROM or extracted game assets. A
user-owned GoldenEye ROM is required.

Implemented Titan boundary:

- TGR v2 in-process session and clean shell restoration
- four logical pads, 32 kHz stereo PCM and 64-bit exclusive storage
- 640x448 double-buffer GS mode with Z16
- colored and textured indexed triangles
- one bounded 1 MiB mixed PSM16/PSM32 GS arena with generation handles,
  deferred reuse and pressure barriers
- PSM16 for binary alpha plus direct RGBA32/PSM32 preservation when required
- two-texture ordered materials, N64 combiner mapping, perspective-correct STQ,
  fog, alpha compare, depth/blend, viewport/scissor and tile wrap/clamp/mirror
- 256 KiB four-page streaming ROM cache for z64/v64/n64 dumps
- exact 12 MiB/header/title validation before boot
- on-demand decoded-SFX LRU cache, native 32 kHz music/SFX path and no
  SDL/pthread dependency in the Titan source set
- DualShock input and rumble through the TGR API
- installable `titan-game` package type and package-gated in-process TGR handoff
- Fast3D clip-space submission path that preserves X/Y/Z/W
- backend-neutral ROM reads used by DMA, audio and blood-animation data
- TitanEYE branding only in `MENU_GOLDENEYE_LOGO`, with the original model as
  fail-closed fallback; `MENU_EYE_INTRO` and `title.c` are unchanged

`mgb64/` is pinned by `MGB64_SOURCE.lock`. The adapter does **not** emulate the N64:
mission logic, collision, weapons, AI and menus remain native C. Only host
services and the Fast3D/RSP/RDP presentation boundary are replaced. GoldenEye
GBI commands therefore require a native Fast3D-to-GS material backend, not an
N64 CPU emulator.

The renderer invariants and the comparison against the SM64 PS2 backend are
recorded in [`docs/TGR_SM64_PS2_AUDIT.md`](../../docs/TGR_SM64_PS2_AUDIT.md).

The source-side port and private package recipe are complete. `Makefile.titan`
compiles the exact native source set, audits the R5900 module and packages only a
validated user-owned NTSC-U ROM. This workspace has no PS2DEV R5900 toolchain,
so no executable TPKG is fabricated here; see `PORT_STATUS.md` for the one
remaining cross-build/hardware validation gate.
