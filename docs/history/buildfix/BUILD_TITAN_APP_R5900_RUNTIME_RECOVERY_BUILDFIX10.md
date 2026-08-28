# Project Titan Magenta Edition 0.3.90-rc14 — Titan App R5900 Runtime Recovery BuildFix10

## Hardware result

BuildFix9 installed `PROJECTNES.pkg` successfully on a physical PlayStation 2.
The installed Project Nes tile and metadata appeared correctly, but pressing X
hard-froze Project Titan before the Titan-native application became usable.

This is a Titan App API failure boundary, not an external ELF handoff failure.
`PROJECTNES` is `type=titan-native` and its entrypoint is the relocatable
`project_nes.tmod`, executed inside the Project Titan EE process.

## Root cause corrected

The original Project Nes preview was produced in a host-validation environment
without the user's PS2DEV/R5900 compiler. Structural ET_REL validation proved
format/relocation shape, but it did not prove that the generated instructions
were valid for the Emotion Engine. A generic MIPS32/MIPS32r2 module is not a
safe in-process Titan App hardware artifact.

BuildFix10 makes R5900 targeting part of the executable Titan App contract:

- runtime execution requires ELF flags for MIPS III + machine 5900;
- ABI must be O32, non-PIC/non-CPIC, without unsupported ASE/FP ABI flags;
- Package Installer performs allocation + relocation runtime validation before
  publishing a Titan-native app;
- structural catalog validation remains separate so an already-installed bad
  module can remain visible long enough to report a precise launch error.

The first-party Project Nes source is integrated under `apps/project-nes/`.
Its Makefile refuses a hardware package unless `EE_CC -dumpmachine` is exactly
`mips64r5900el-ps2-elf`, and compiles with `-march=r5900 -mabi=32`.
`scripts/build_all.sh` builds the package with the same PS2DEV EE compiler used
for the Titan kernel and publishes it at:

`dist/Project Titan Magenta Edition/TITAN/packages/PROJECTNES.pkg`

The old host-generated Project Nes package must not be reused for this test.

## Runtime containment and recovery

Titan-native launch no longer calls `titan_app_init()` synchronously from the
shell frame that handled X.

Launch is a phased state machine:

1. park the background storage worker;
2. validate/load/relocate the R5900 `.tmod`;
3. load optional background;
4. start `titan_app_init()` on a dedicated 64 KiB EE thread;
5. keep the shell rendering a launch status panel while init runs;
6. bridge init-time allocation, notification, file and media-control requests
   back to the main runtime owner;
7. enforce a 15-second init watchdog;
8. allow Select + Circle to cancel launch and recover Home.

Storage remains single-owner for the full Titan App session so scanner, asset,
VMC and music-prefetch work cannot race app filesystem calls.

Project Nes 0.1.1 also defers its optional `data/roms/game.nes` probe from
`titan_app_init()` to the first update. The built-in diagnostic ROM remains in
the package.

## Loader hardening

Runtime relocation validation now rejects malformed relocation tables before
execution, including:

- relocation sections linked to the wrong symbol table;
- misaligned/out-of-range relocation offsets;
- unsupported relocation types;
- invalid symbol indices;
- dangling R_MIPS_HI16 pairs;
- required exports that are not aligned STT_FUNC symbols in allocated,
  executable sections.

Supported runtime relocations remain R_MIPS_32, R_MIPS_26, R_MIPS_HI16 and
R_MIPS_LO16.

## Validation performed here

- `bash kernel/tests/run_host_tests.sh`: PASS
- `bash scripts/validate_source.sh`: PASS
- Titan App structural generic-MIPS fixture: PASS
- generic MIPS32r2 runtime execution guard: rejected as intended
- R5900-contract fixture allocation/relocation validation: PASS
- Project Nes host 6502/PPU diagnostic test: PASS
- Project Nes host PCM/APU diagnostic test: PASS
- Project Nes compiler guard rejects a non-PS2 compiler: PASS
- EE-conditional runtime source syntax checked with strict host stubs: PASS

The real `mips64r5900el-ps2-elf-gcc` toolchain is not installed in this
container, so the final Project Nes `.tmod`, `PROJECTNES.pkg`, kernel ELF and
hardware execution are intentionally produced/tested on the user's PS2DEV
machine.

## Hardware test

Run `bash scripts/build_all.sh`, deploy the resulting Titan build, remove the
old installed Project Nes preview, and install the newly generated
`TITAN/packages/PROJECTNES.pkg` from the runtime output. Then launch its tile.

If launch is rejected, the notification now states the exact loader/target
failure instead of jumping into an unsafe module. If init stalls, the launch
screen remains alive and the watchdog/Select+Circle recovery path returns to
Home.
