# BuildFix26 Target Build Report

## Current execution

Status: **BLOCKED / NOT_EXECUTED**

The BuildFix26 host environment does not contain:
- `mips64r5900el-ps2-elf-gcc` and companion binutils;
- a configured `PS2DEV` / `PS2SDK` tree;
- `dvp-as` / `dvp-objcopy`;
- PCSX2.

Therefore no R5900 ELF, linker map, target section sizes, IOP memory measurement or VU target artifact is claimed as validated in this execution.

## Reproducible target probe

Run from the BuildFix26 source root on a PS2DEV environment:

```sh
./tools/buildfix26_target_build.sh ./buildfix26-target-report
```

The script intentionally exits status 2 with `STATUS=BLOCKED` if any required toolchain component is missing. When available it:
1. cleans and builds `kernel/kernel.elf` with the exact source tree;
2. records ELF SHA-256;
3. records aggregate and per-section size (`.text`, `.rodata`, `.data`, `.bss` and all other emitted sections);
4. records ELF header, symbol table and relocations;
5. records undefined symbols for review;
6. verifies the Ribbon VU1 DVP object and binary;
7. inventories produced IOP `.irx` artifacts.

## Required target review before TARGET_BUILD_VALIDATED

- no unexplained undefined/duplicate symbols;
- no target-only warnings hidden by the build;
- VU1 microprogram object/binary present and non-empty;
- IOP modules expected by Network/Storage present;
- section alignment and relocation output reviewed;
- EE image size compared with BuildFix25 target image if a material target baseline exists;
- `.bss` and heap/stack coexist safely within EE RAM;
- EE RAM results must never be combined with GS VRAM accounting.

Only after build completion **and** report review may the status become `TARGET_BUILD_VALIDATED`.
