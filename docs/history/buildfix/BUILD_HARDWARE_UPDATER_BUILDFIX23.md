# Project Titan Magenta Edition RC14 — BuildFix23

Hardware-focused correction for the TPUP updater after physical PS2 tests repeatedly failed with `Checksum da atualizacao invalido`.

## Root cause addressed

The updater transaction began authoritative FileXio work before the background storage worker had acknowledged package-exclusive mode. During extraction it also kept the PUP input file and the staging output file open at the same time on the same USB mass device. That pattern is intentionally avoided elsewhere in the current Package Installer because real PS2 FileXio/usbhdfsd behavior has already shown sensitivity to multi-owner / multi-handle access.

## New transaction model

1. `TitanSystemUpdate_Begin()` performs **no filesystem I/O**. It only records the selected package and enters `TITAN_SYSTEM_UPDATE_PREPARING`.
2. The storage scheduler parks its worker and only then calls `TitanSystemUpdate_Update()` from the main EE owner.
3. PREPARING re-inspects header/table/build while storage is exclusive.
4. VERIFYING performs a full read-only CRC pass over every PUP payload entry before any staging mutation.
5. EXTRACTING uses a single-handle pattern per chunk:
   - open PUP -> exact seek -> read -> close;
   - open staging file -> exact seek/write -> close.
   The input PUP and staging output are never live simultaneously.
6. Every staged file is reopened and checked again for exact size + CRC before publication.
7. Publication keeps the existing rollback-safe atomic replace behavior.

Checksum errors now identify the failing layer. Examples:

- `CRC PUP invalido arq N exp XXXXXXXX obt YYYYYYYY` = bytes read from the source PUP do not match the package table.
- `CRC durante extracao invalido ...` = stream CRC diverged while extracting.
- `CRC do staging divergiu apos reabrir arquivo` = source read passed but bytes written/re-read from staging differ.

## Build / bootstrap

`TITAN_SYSTEM_UPDATE_CURRENT_BUILD` is now **23**.

Because the broken updater lives inside the already-running BuildFix20/21/22 kernel, it cannot reliably update itself. BuildFix23 therefore requires one manual bootstrap:

1. compile BuildFix23;
2. replace the installed `kernel.elf` once by the normal USB/manual deployment method;
3. reboot and confirm the UI reports build 23.

After that, use the tiny PUP hardware self-test before attempting a real system update.

## PUP hardware self-test

A 354-byte self-test package is provided separately as:

`Project-Titan-PUP-SelfTest-build24.pup`

It contains only:

`TITAN/update-tests/build24.ok`

Expected result on BuildFix23: update completes and publishes that 146-byte marker file. This isolates updater transport/staging from kernel/app complexity.

The source also provides a reproducible target:

`make -C kernel pup-selftest`

(or call `kernel/tools/titan_pup.py` directly).

## Regression validation performed here

- forced 3-byte short reads/writes: PASS
- read-only source-payload CRC preflight: PASS
- deliberately corrupted payload detected before publish: PASS
- multi-file publication + rollback: PASS
- wLaunchELF host lifecycle: PASS
- wLaunchELF native-port contract: PASS
- BuildFix23 hardware-updater source contract: PASS
- TPUP self-test package verify: PASS

The execution environment does not contain PS2DEV, so the MIPS `kernel.elf` must still be compiled on the user's PS2DEV host.
