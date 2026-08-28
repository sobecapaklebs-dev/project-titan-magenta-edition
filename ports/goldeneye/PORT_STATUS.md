# TitanEYE port status — source-complete release candidate

## Completed and host-validated

- TPKG v2 defines `type=titan-game` with an R5900 ET_REL `.tmod`, install
  manifest, Home launch routing and package-gated in-process TGR handoff.
- `game_begin/game_end` preserves existing Titan App ABI offsets and restores
  GS, audio, input and storage ownership before returning to the shell.
- TGR v2 exposes 640x448 double buffer, Z16, clip W, perspective STQ, two
  textures, combiner modes, fog, alpha compare, depth/blend, viewport/scissor,
  sampler state, mixed PSM16/PSM32 uploads, four pads, rumble, 32 kHz PCM,
  64-bit storage and a 12 MiB heap.
- The single 1 MiB GS texture arena uses generation handles and defers reuse
  until GS `FINISH`; the Fast3D logical ID table is reusable under long-running
  cache churn.
- Indexed, clip-space and material lanes share one mathematically tested
  partial-viewport transform. ST/Q includes NPOT correction and packed GIF lane
  tests; RGBA32 is retained whenever alpha is not binary.
- The Fast3D backend maps GoldenEye material state to TGR without
  OpenGL/WebGPU/Metal. Two-texture materials use deterministic ordered GS
  passes.
- SDL/POSIX presentation, controller, audio device and worker dependencies were
  replaced in the Titan target. Optional desktop diagnostics are closed no-ops,
  not privileged imports.
- ROM DMA, audio and blood-animation data use a 256 KiB four-page cache. The
  entire 12 MiB dump is never copied into EE RAM.
- SFX decode is on demand into a 1.5 MiB LRU that never evicts active voices;
  music keeps the original streaming/libaudio path.
- The game pool is 5 MiB, renderer queues flush at bounded limits, the pointer
  registry uses 4096 entries and N64 framebuffer symbols are sentinels because
  the GS owns the actual surfaces.
- Fatal allocation paths request cooperative shutdown. Rumble, audio, ROM
  handles, textures and TGR ownership are closed on every module exit path.
- Branding is restricted to `src/game/front.c` and the
  `MENU_GOLDENEYE_LOGO` branch. `title.c`, `MENU_EYE_INTRO`, timings, camera,
  music and SFX remain outside the branding change.
- The pinned MGB64 base is commit
  `cd9b58f5f91291579b8e551aa925aab000d311cf`.

## Validation evidence

- The exact 222-source Titan list compiles with host GCC and links into one
  relocatable audit image.
- Audit image: 1,554,164 bytes text, 234,528 data and 4,276,728 BSS;
  6,065,420 bytes total, 2,323,188 bytes below the Titan 8 MiB relocated-image
  limit.
- Remaining host undefined symbols are the approved pure `libc/libm` surface;
  `_GLOBAL_OFFSET_TABLE_` and `__isoc23_strtoull` are host ABI artifacts. The
  R5900 build disables PIC and resolves the normal `strtoull` name.
- Package, installer, loader, TGR, renderer, ROM cache, platform bridge and
  Ribbon contract suites pass under the host toolchain.
- The supplied ROM validates as the supported 12 MiB NTSC-U z64 dump with
  SHA-1 `abe01e4aeb033b6c0836819f549c791b26cfde83`.

## Remaining external gate

This environment does not contain `mips64r5900el-ps2-elf-gcc`/`ld`, PCSX2 or a
physical PS2. Therefore the final R5900 relocation table, GS/SPU2 timing and
hardware gameplay cannot be truthfully certified here.

In a PS2DEV environment, the private package is produced directly with:

```text
make -C ports/goldeneye -f Makefile.titan package ROM="/path/to/GoldenEye 007.z64"
make -C ports/goldeneye -f Makefile.titan verify ROM="/path/to/GoldenEye 007.z64"
```

Expected output: `ports/goldeneye/build/titan/TITANEYE007.tpkg`. No dummy,
host-architecture module or missing-ROM launcher is accepted as a substitute.
