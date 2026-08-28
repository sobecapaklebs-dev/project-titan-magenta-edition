# BuildFix27 Pre-Flight — Native Application Integration

This document is a plan, not an implementation authorization inside BuildFix26.

## Gate 0 — target proof before native-app expansion

Before changing app architecture:
1. Build the exact BuildFix26 tree with PS2DEV/R5900 and VU tools.
2. Cold-boot Reference 720p on hardware and capture BuildFix26 memory watermarks.
3. Execute the 10-cycle shell/theme/TitanEYE lifecycle test.
4. Validate Network/Storage under active UI.
5. Only then begin BuildFix27 application work.

## 1 — wLaunchELF native integration

Preserve the existing `.tmod` and Titan ownership model. Import/adapt proven upstream algorithms rather than importing its standalone event loop, gsKit ownership, pad loop or IOP initialization.

Priority adapters:
- recursive copy/move with bounded cooperative jobs;
- rename/new-folder UI using the already available Titan App API;
- explicit PFS/HDD capabilities and mount lifecycle via Titan Storage/VFS;
- VMC service adapter;
- text editor and JPG viewer as Titan-rendered features;
- FTP only after the public Titan Network Client API exists;
- preserve validated Titan stage2 ELF launch.

Licensing gate: retain upstream AFL-2.0 attribution/notices for imported code.

## 2 — Project NES core replacement

Keep: Project NES tile/frontend, Titan App lifecycle, Titan media/input/storage backend and shell return semantics.
Replace: the preview emulator core.

### Candidate evidence

**FCEUmm-PS2** (`ps2homebrew/Fceumm-PS2`) is a real PS2 source port, GPL-2.0, built around PS2SDK/gsKit and explicitly derived from FCE Ultra mappers-modified. Its source tree separates core-style files (`x6502.c`, `ppu.c`, `sound.c`, `ines.c`, `boards/`) from `drivers/`, making it useful as a PS2 target-adaptation reference.

**Current libretro FCEUmm** (`libretro/libretro-fceumm`) is GPL-2.0 and documents broad mapper coverage, NES/Famicom/FDS/Dendy/VS support, region detection and expansion audio. It is the preferred functional/correctness reference because it contains later core work than the historical PS2 fork.

### Selection strategy

Do not blindly vendor either complete frontend.
1. Pin exact commits of both references.
2. Build FCEUmm-PS2 unchanged with the same PS2DEV toolchain to establish R5900 viability.
3. Isolate the emulator core from PS2 `drivers/` and replace platform callbacks with a small Titan backend.
4. Diff core files against current libretro FCEUmm and determine which newer mapper/timing/audio changes are safe to carry into the PS2-buildable core.
5. Produce host ROM-free conformance tests and target performance measurements before replacing Project NES 0.1.5.

Target architecture:
`Project NES frontend -> Titan App Runtime -> FCEUmm-derived core -> Titan video/audio/input/storage backend`.

Licensing gate: FCEUmm is GPL-2.0. A distributed linked `.tmod` containing that core must be packaged with the required GPL source/license obligations. Do not silently relabel the combined emulator module as MIT. This does not by itself relicense the separate Titan kernel/API, but distribution boundaries must be reviewed before public release.

## 3 — Browser + Titan Network Core

Keep the current kernel-resident Browser/parser/UI. Replace direct transport ownership with a reusable Titan network client layer.

Needed service surface:
- network-ready/status/capabilities;
- asynchronous DNS;
- bounded TCP connect/send/recv/cancel;
- HTTP request/response helper or stream adapter;
- explicit timeouts and cancellation;
- TLS/HTTPS capability negotiation when a target-safe TLS backend exists;
- download sink through Titan Storage, never browser-private FileXio ownership.

The Browser becomes the first demanding consumer/test of the public network API. AR9271/Ethernet/DHCP/DNS remain owned by Titan Network Core.

## 4 — Contracts required in BuildFix27

- wLaunchELF recursive job/lifecycle/storage capability contracts;
- no direct FileXio/IRX/gsKit/pad ownership in native wLaunchELF;
- Project NES core/frontend separation contract;
- mapper/region/SRAM/audio timing conformance fixtures without copyrighted ROMs;
- Browser no-direct-PS2IP boundary once Titan Network Client API lands;
- network cancel/timeout/storage-download contracts;
- app lifecycle leak cycles using BuildFix26 watermark format;
- SAS/runtime classification independence.

## Primary references audited

- https://github.com/ps2homebrew/wLaunchELF
- https://github.com/ps2homebrew/Fceumm-PS2
- https://github.com/libretro/libretro-fceumm
- https://fceux.com/web/help/Gamefilecompatibility.html (secondary comparison/reference for FCEU family capabilities; not selected as a PS2 frontend)
