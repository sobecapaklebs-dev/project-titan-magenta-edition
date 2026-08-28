# Project Titan Magenta Edition 0.3.90-rc14 — BuildFix12

## Titan App containment / Project Nes 0.1.2 / VU1 PATH1 synchronization

### Physical-hardware symptom that motivated this build

On a real PlayStation 2, Project Nes could install, launch and return to Home, but the image became severely corrupted immediately after the Titan App launch phase changed from `Inicializando aplicativo` to the active application. The shell itself remained responsive and returned to a normal image after app exit. DEX also reported `VU1 FALLBACK`.

The important boundary is that the Project Nes library screen does **not** call `media_draw`; it only uses Titan App UI primitives. Therefore BuildFix11's RGB565 media conversion was not sufficient to explain the new corruption and both the app and the platform were audited.

### Titan App runtime hardening

- Removed process-global `TitanVideo_SetTransform()` from foreign module execution. Windowed coordinates are now validated and mapped by each public Titan App drawing primitive before reaching the renderer.
- App rectangles must remain inside the public 854x448 canvas; non-finite/out-of-contract geometry is rejected rather than submitted to GS.
- Added per-session allocation accounting (32 tracked allocations, 4 MiB aggregate cap).
- `release()` only frees exact app-owned allocations. Unknown pointers are rejected instead of being passed to libc `free()`.
- Outstanding Titan App heap allocations are reclaimed automatically when a session closes or launch aborts.
- File/media buffer spans are accepted only when they reside in tracked Titan allocations or allocated module sections with the required access.
- App strings used by UI/path/notification APIs are bounded and must reside in known app memory.
- PCM and RGB565 submissions now validate complete source spans before reading them.

### Project Nes 0.1.2 audit and correction

The previous `.tmod` placed the complete emulator state plus 256x240 and 512x256 framebuffers in ET_REL `.bss`. A host MIPS audit build measured approximately 0x63a44 bytes (~408 KiB) of `.bss` before this change.

BuildFix12 leaves only the Project Nes state pointer in module `.bss`. `ProjectNesState`, the NES framebuffer, the Titan 512x256 RGB565 surface, audio staging and ROM storage are Titan-owned heap allocations. A host MIPS audit object now reports `.bss` size 0x000004 (4 bytes).

Additional fixes:

- removed the init-time success notification from the critical transition into the app;
- releases rejected/unsupported ROM buffers immediately;
- initializes all runtime surfaces explicitly before first use;
- shutdown releases ROM, audio, media, NES frame and state in deterministic order;
- added a full app lifecycle host test with guarded allocations, library draw, ROM launch, RGB565 submit, PCM submit, running-screen draw and shutdown/leak verification.

### VU1 / PATH1 correction

The previous handoff used `TitanVideo_SubmitPacket(0)` before VU1 PATH1/XGKICK. That waits for GIF DMA feeding but does not place a GS `FINISH` synchronization point before PATH1 ownership. BuildFix12 changes `TitanVideo_FlushForPath1()` to submit a FINISH and wait for GS completion before VIF1 launches the ribbon microprogram.

VU1 DMA/status wait budgets were also enlarged and an isolated transient draw synchronization failure no longer permanently disables the VU backend immediately; three consecutive failures are required before the safe CPU fallback becomes permanent for the session. A successful draw clears the transient counter.

The VU microprogram and wave texture assets were not modified.

### Validation completed in this environment

- `scripts/validate_source.sh`: PASS
- Titan App ET_REL/R5900 loader tests under ASan/UBSan: PASS
- Package Installer transaction tests: PASS
- renderer 480i/720p contracts: PASS
- Titan App window/media contract: PASS
- XMB spline host/runtime contracts: PASS
- Project Nes host core test: PASS
- Project Nes host audio test: PASS
- Project Nes full lifecycle guarded-allocation test: PASS
- Project Nes lifecycle under ASan/UBSan: PASS
- Project Nes core under ASan/UBSan: PASS
- Project Nes audio under ASan/UBSan: PASS
- GCC `-fanalyzer` on `project_nes.c`: no diagnostics
- host MIPS audit `.bss`: 4 bytes after the heap-state conversion

### Asset preservation

All 255 files covered by the release asset manifest (`*/assets/*` plus runtime Magenta audio) were hashed before and after BuildFix12. The manifests compare byte-for-byte identical.

No UI image, icon, font asset, boot image, audio asset, wave texture source asset or other curated asset was intentionally changed.

### Hardware status

The code paths above are host-audited and compile-contract validated, but this environment has no PS2DEV/R5900 toolchain or physical PlayStation 2. The final proof is the user's next real-hardware build/test. `build_all.sh` will build Project Nes 0.1.2 with the user's `mips64r5900el-ps2-elf-gcc` and place the fresh `PROJECTNES.pkg` in the runtime package.
