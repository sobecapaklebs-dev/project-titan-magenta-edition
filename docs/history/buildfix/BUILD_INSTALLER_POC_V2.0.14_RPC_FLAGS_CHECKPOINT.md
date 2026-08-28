# Project Titan Magenta Edition — Installer POC v2.0.14

## Scope

Hardware-log correction only. No installer visual, theme, wave geometry, TINS v1 format, payload or installation-policy changes.

## Physical evidence: INSTALLER(3).LOG / v2.0.13

The first TINS entry reached staging successfully, and the v2.0.13 CRC seed correction held. The run then exposed three independent defects:

1. `ASYNC_OPEN_STAGE_WRITE` completed with descriptor `3`, but `ASYNC_WRITE_STAGE` completed with `0` bytes. The direct FileXio path was receiving newlib/POSIX `O_*` flags even though FileXio's IOP ABI uses the distinct `FIO_O_*` flag domain.
2. A 3283 ms wave gap occurred immediately after `CHECKPOINT label=install-start`, with `install_pump_ms=0` and no blocking I/O attributed to the frame. `TitanInstallerDiag_FlushCheckpoint()` was synchronously writing the entire RAM log snapshot to the same USB device before payload work began.
3. Diagnostics counted the open result `fd=3` as three written bytes because byte accounting matched `WRITE` in the operation name without requiring a non-zero requested transfer size.

The log also showed small 33 ms frames around synchronous TINS reads/metadata probes and false `IO_FAILURE` noise for normal first-install/non-existent transactional paths.

## v2.0.14 corrections

- Added explicit libc `O_*` -> IOP `FIO_O_*` translation before every direct `fileXioOpen()` and direct `mcOpen()` call.
- Re-enabled forward-only `mass:` TINS/data reads through FileXio `FXIO_NOWAIT`, now with the correct access flags; completion is polled between rendered frames. There is still no second EE filesystem worker.
- `mass:` staging output remains FileXio `FXIO_NOWAIT`; POSIX output remains only the safe fallback when FileXio OPEN itself cannot be established.
- Memory Card continues to use asynchronous libmc and receives the translated IOP open flags.
- Mid-install diagnostic checkpoints are RAM-only. Persistent log writes occur at final success/failure, not at `install-start`.
- Diagnostic transfer-byte accounting now requires `requested_bytes > 0`, so an open fd cannot be counted as payload bytes. Completed zero-byte READ/WRITE RPCs are emitted explicitly as `ASYNC_ZERO_TRANSFER`.
- Missing `INSTALLER.CONSUMED` and negative `STAT` existence probes during transactional phases no longer become false error events.
- Main-thread synchronous metadata budget reduced from 10 ms / 24 steps to 4 ms / 6 steps. Payload data RPCs are asynchronous, so this does not throttle USB data to one tiny block per rendered frame.
- v2.0.13 CRC zero-seed fix, per-entry recovery, touched-prefix rollback and TINS format are unchanged.

## Expected hardware signatures

On the next run:

- `OPEN_FLAG_TRANSLATION backend=filexio ...` appears once and shows distinct POSIX/IOP flag values.
- `ASYNC_WRITE_STAGE` for `TITAN/audio/README.md` must complete with `result=1018`, not zero.
- There must be no multi-second `install-start` checkpoint frame; `CHECKPOINT` reports `persistence=ram-only`.
- `ASYNC_READ_TINS` service latency may exceed a frame, but it must report `frame_blocking=0` through the async aggregate path and the waves should continue rendering while the IOP works.
- The v2.0.13 source CRC fix remains intact; README expected CRC is `0x0412C4D9`.
