# RC14 Package Installer — Single-owner FileXio + RAM preload BuildFix9

Hardware symptom addressed: the Package Installer remained at 0% / "Preparando" after package preparation was moved to the dedicated EE storage worker.

## Root-cause direction

The Package Installer catalog already opens and validates each TPKG successfully while the UI enumerates packages. BuildFix8 then repeated package stat/open/header/table I/O from the separate storage worker after the install click. On real hardware that path regressed to 0% before payload extraction.

BuildFix9 stops moving Package Installer filesystem calls between EE threads. Package mode is now a deterministic single-owner critical section:

1. the shell enables package-exclusive mode and suspends music/background storage;
2. the storage worker acknowledges a parked state;
3. only after that acknowledgement does the main/UI thread issue package filesystem operations;
4. one bounded package step is executed per frame;
5. the worker remains parked until the Package Installer exclusive session is released.

This intentionally favors hardware reliability over perfectly smooth frame pacing during installation.

## Cached package plan

Package enumeration already validates the complete TPKG header and file table. BuildFix9 keeps those validated structures in EE RAM (about 300 KiB at the 32-package maximum) and reuses them after the user presses Install.

The install path therefore no longer repeats package stat/header/table reads before 1%. It begins with destination/staging preparation and payload opening.

## Small-package RAM preload

For payloads up to 1 MiB, the source payload is read completely into an aligned EE RAM cache before extraction. The source `.pkg` descriptor is then closed and installation becomes destination-only I/O.

This changes the common small-app path from:

`USB read package -> USB write staging -> USB read package -> USB write staging ...`

to:

`USB read payload once -> close source -> USB write staging`

Packages above 1 MiB retain the bounded 4 KiB streaming fallback.

## Preserved behavior

- Fresh generation staging (`.install-TITLEID-GENERATION`) remains transactional.
- No recursive staging cleanup is performed on the interactive install path.
- No per-file fsync is restored.
- Circle remains request-only cancellation.
- Package-exclusive mode still pauses scanner, VMC, asset build and music storage work until the user exits Package Installer.
- Existing renderer/UI/audio assets are unchanged by this BuildFix.

## Validation

`kernel/tests/run_host_tests.sh` passes in full, including:

- package transaction tests under ASan/UBSan;
- TPKG builder/verifier;
- artificially limited partial storage transfers;
- release contract;
- source hardening contract;
- audio runtime/contract;
- Home/DEX/video/renderer contracts.

The package tests are small enough to exercise the new RAM-preload path.

A host syntax-only compile of `titan_storage_scheduler.c` with the project include graph also passes with `-Wall -Wextra -Werror`.

Real PS2 hardware validation is still required. This environment does not provide the PS2DEV/PS2SDK R5900 toolchain, so no claim is made that a `kernel.elf` was produced here.
