# Project Titan Magenta Edition 0.3.90-rc14 — BuildFix15

## Titan App pure-init boundary / Project Nes 0.1.5

### Physical-PS2 symptom

Project Nes 0.1.4 still hard-froze at `Inicializando aplicativo`: no controller input, no watchdog recovery and no storage activity. This persisted after BuildFix14 rejected unsupported R5900/COP1 instructions, so the ISA hypothesis was not sufficient.

### Regression isolated by source comparison

The earlier Project Nes path that did cross `titan_app_init()` on real hardware kept its large emulator state/framebuffers in module-local static storage. BuildFix12 moved those surfaces to `TitanAppApiV1::alloc()` from the protected init thread and introduced a cross-thread request bridge so the main thread would service heap/storage/media work on behalf of `titan_app_init()`.

That bridge is exactly on the hardware freeze boundary. BuildFix15 removes the bridge instead of attempting another timing workaround.

### Titan App runtime changes

- `titan_app_init()` is now a pure launch handshake.
- During init, heap allocation/release, storage, notifications and media/audio APIs are rejected immediately; there is no shared request/wait bridge to the main thread.
- Drawing APIs remain inert while `launching` is true.
- After init returns successfully the runtime changes to ACTIVE; normal API resource calls then execute on the normal Titan app update path.
- The 15-second init watchdog, R5900 ISA guard, relocation validation, window/fullscreen containment and session cleanup remain intact.
- Source-hardening contracts reject reintroduction of the init bridge and reject Project Nes init-time resource calls.

### Project Nes 0.1.5

- `ProjectNesState`, NES 256x240 RGB565 frame, Titan 512x256 RGB565 frame and PCM staging are module-local static aligned storage again.
- `titan_app_init()` only validates/stores the API pointer, clears module-local state and selects the library screen.
- No `alloc`, `release`, file, notification, audio or media API is called from init.
- The optional `game.nes` probe stays deferred to the first ACTIVE update.
- ROM payload allocation occurs only after the user presses X, when the app is already ACTIVE.
- BuildFix14 single-float/R5900 ISA checks, PPU warm-up, explicit RGB565 error handling and fixed-step audio/video logic remain.

### Validation

- `make -C apps/project-nes host-test`: PASS
  - 6502/PPU core: PASS
  - PCM/APU: PASS
  - full lifecycle: PASS (`media=13 audio=6934 draw=10`)
- actual 0.1.5 `.tmod` R5900 ISA scan: PASS (5196 instructions checked)
- actual 0.1.5 `.tmod` runtime allocation/relocation validation under ASan/UBSan: PASS
- TPKG v2 package verify: PASS
- `scripts/validate_source.sh`: PASS
- Titan App ET_REL/R5900 ISA guard: PASS
- Titan App window/media contract: PASS
- VU1 no-internal-edges contract: PASS
- source-hardening contract: PASS

### Assets

No visual/audio asset was modified.

- `kernel/assets`: 241 files, byte-for-byte identical to BuildFix14
- `bootloader/assets`: 3 files, byte-for-byte identical to BuildFix14

### Build note

The source Makefile still requires `mips64r5900el-ps2-elf-gcc` for canonical hardware packages. The ready-to-install 0.1.5 package supplied for this test was produced in this environment with Clang MIPS III/O32, `-msingle-float`, stamped with the R5900 machine contract, then passed through the same ISA scanner and runtime validator used by BuildFix15. Final proof remains the physical PS2 test.
