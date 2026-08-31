# Project Titan Installer POC v2.0.13 — hardware-log fixes

Scope: transaction/I/O/recovery diagnostics only. No UI/theme/wave geometry changes. TINS v1 unchanged.

## Evidence from INSTALLER(2).LOG

- v2.0.12 reached entry 1/84 and the payload stream correctly.
- `TITAN/audio/README.md` is 1018 bytes. Manifest CRC was `0x0412C4D9`; runtime produced `0x7D7F0893`.
- Reproducing the installer CRC locally shows `0x0412C4D9` with seed `0`, and exactly `0x7D7F0893` with the erroneous seed `0xffffffff`.
- The first 1018-byte POSIX FAT staging write blocked the EE thread for 1623 ms and produced a 1650 ms wave frame.
- The pre-install recovery sweep still generated repeated metadata probes and rollback after the first-entry failure unnecessarily touched unrelated BOOT state.
- Parent `mkdir` calls returned errno 8 even though subsequent file creation proved the path usable.

## v2.0.13 changes

1. CRC accumulator seeds restored to `0` for source, existing-file and staging CRCs.
2. Sequential TINS reads remain POSIX/newlib and seekless.
3. `mass:` output open/write/close now uses FileXio `FXIO_NOWAIT`, polled between rendered frames. FileXio READ is deliberately not used for TINS.
4. Memory Card output remains native asynchronous libmc.
5. FileXio output-open submission/completion failure can fall back safely to POSIX before any payload bytes are committed.
6. Full-manifest startup recovery sweep disabled; stale `.TITAN.NEW` is handled on demand per entry.
7. Rollback scope is limited to the touched/published manifest prefix instead of all 84 entries.
8. Unchanged entries are skipped by rollback.
9. Stale staging deletion failure is nonfatal; the subsequent `O_TRUNC` output open is authoritative.
10. Parent mkdir failures are telemetry-only (`MKDIR_OPTIONAL`); output open determines whether the path is actually unusable.
11. `INSTALL.COMPLETE` is written via the same async output path.

The robust diagnostic logger is retained.
