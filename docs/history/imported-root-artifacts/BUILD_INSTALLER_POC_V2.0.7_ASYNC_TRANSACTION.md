# Project Titan Installer POC v2.0.7 — async transaction engine

This build addresses two hardware findings from the first physical installation tests:

1. Large payloads were paced by the UI frame loop (8 KiB per frame) and large assets were read three times (TINS source, staging write, staging CRC reread).
2. Publication could fail on `rename(destination, destination.TITAN.BAK)` even though normal read/write access to the same USB device worked.

## v2.0.7 changes

- Installer transaction runs on a low-priority EE I/O worker thread (priority 96).
- Main/render thread never waits for the installer storage state machine except in degraded thread-creation fallback mode.
- Transfer buffer/slice increased to 128 KiB.
- Large non-critical files use source CRC + exact staged size verification and do not perform the third full staging read.
- Boot/exploit, files <= 4 MiB, and executable/package module extensions retain full staging CRC verification.
- Existing files with matching size are CRC-checked before staging. If the CRC already matches the TINS entry, the destination is not rewritten.
- The corresponding TINS payload is still consumed and CRC checked, preserving installation-media integrity while avoiding an unnecessary write.
- Publication first uses POSIX `rename()`, then path-only `fileXioRename()` compatibility fallback.
- If rename is unavailable, preservation/publication can fall back to streamed copy on the I/O worker.
- Rollback/recovery use the same compatibility helpers.
- Publish failures now include the entry path plus POSIX errno and fileXio result.
- Progress is byte-based and monotonic.

## Expected impact on Magenta Melody

The current runtime asset `runtime/TITAN/audio/magenta_melody.wav` is about 108 MiB.

v2.0.6 could perform three complete passes and was also artificially limited by the render loop. v2.0.7 removes the frame-rate pacing. For a new copy, large media uses one source read plus one staging write; if an existing destination already has the expected CRC, it is not rewritten.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Output:

```text
titan-installer.elf
```

The existing `TITAN_SYSTEM.TINS` does not need to be regenerated for this revision.
