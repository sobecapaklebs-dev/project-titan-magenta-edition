# Project Titan Magenta Edition RC14 — BuildFix14

## Scope

BuildFix14 addresses a physical-PS2 hard freeze observed while launching Project Nes 0.1.3. The last visible shell frame remained on `Inicializando aplicativo`; input stopped responding and storage activity stopped.

## Root cause

The ready-made Project Nes 0.1.3 package had been produced with a generic Clang MIPS III/O32 path and then stamped for R5900 execution. Inspection of the actual `.tmod` showed `SDC1`/`LDC1` in `titan_app_draw()`. Those double-width COP1 load/store opcodes are not part of the EE/R5900 COP1 execution set. Because Titan-native apps execute in-process, the invalid instruction could stop the entire shell before the next frame was presented.

## Titan App runtime hardening

- Runtime validation now scans every `STT_FUNC` range in executable PROGBITS sections before allocation/relocation/execution.
- `LDC1` and `SDC1` are rejected.
- COP1 encodings are restricted to the R5900 single-precision/transfer/branch subset used by the EE.
- Structural discovery remains permissive so a bad installed app can still be shown/uninstalled; execution validation is strict.
- The host suite includes both a safe single-float R5900 fixture and an R5900-marked fixture containing unsupported double COP1 code. The latter must be rejected.

## Project Nes 0.1.4

- Hardware Makefile explicitly uses `-mhard-float -msingle-float` in addition to `-march=r5900 -mabi=32`.
- Added `tools/check_r5900_tmod.py` to scan the final module before package verification.
- Added a three-frame ROM/PPU warm-up state; black boot frames are not published as the first media frame.
- RGB565 presentation failure now becomes a visible application error instead of a silent black screen.
- Emulation is fixed-step at 60 Hz using `delta_ms`, with up to three catch-up NES frames per Titan update.
- Each emulated frame produces its own 533/534-frame PCM slice, decoupling audio rate from shell update rate.
- Pause stops the external PCM stream; resume/reset restarts it cleanly.

## Validation

- `scripts/validate_source.sh`: PASS
- Titan App ET_REL/R5900 ISA guard validation: PASS
- Project Nes core test: PASS
- Project Nes audio test: PASS
- Project Nes lifecycle test: PASS
- Actual 0.1.4 `.tmod` accepted by BuildFix14 runtime validator: PASS
- Project Nes `.tmod` ISA scan: 5689 instructions checked, PASS
- TPKG v2 verify: PASS
- `kernel/assets`: 241 files byte-for-byte identical to BuildFix13
- Asset manifest SHA-256: `6a101ca6d23299266380f93e537562c2bf1d9631511f6dbeb9fef76201f56e80`

## Hardware note

This environment does not contain the PS2DEV `mips64r5900el-ps2-elf-gcc` toolchain, so the Project Nes 0.1.4 ready-to-install test package was produced with Clang MIPS III/O32 in explicit single-float mode, then stamped with the R5900 machine/ABI metadata and scanned by the same ISA rules now enforced by BuildFix14. The source Makefile continues to require the real PS2DEV EE compiler for canonical releases.
