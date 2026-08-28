# Project Titan Installer POC v2.0.11 — Async RPC + mcGetDir + Diagnostic Aggregation

This revision is based directly on the real-hardware `INSTALLER.LOG` produced by v2.0.10.
It does not alter the Installer visual design, TINS v1 format, themes, waves, assets, or navigation.

## Hardware findings addressed

The captured session showed:

- install failure at entry 84/84 (`titan.elf`) while allocating a BOOT backup;
- 66 filesystem probes in that allocation frame and a false "no backup slot" result;
- 32 KiB USB reads taking about 57–58 ms;
- 32 KiB Memory Card writes taking hundreds of milliseconds;
- rollback `remove()` of `magenta_melody.wav` blocking the EE for about 7.3 seconds;
- 30k+ diagnostic lines dropped because repetitive stutters flooded the 128 KiB ring.

## v2.0.11 transaction changes

### 1. BOOT backup selection via libmc directory enumeration

For BOOT publication, numbered backup selection no longer performs `stat()` for every candidate.
The Installer starts `mcGetDir(port, 0, "/BOOT/*", ...)`, polls completion with `mcSync(1, ...)`,
and selects the canonical or first free `BOOT.ELF.TITAN.BKxx` name from the actual directory entries.

### 2. Single-owner asynchronous FileXio RPC state machine

The Installer still has one filesystem owner on the main EE thread. It does **not** create a second
FileXio worker thread.

Potentially long FileXio calls are issued with `FXIO_NOWAIT` and completion is polled with
`fileXioWaitAsync(FXIO_NOWAIT, ...)` on later frames. Only one FileXio RPC may be outstanding.

Async transaction operations cover:

- container/staging opens;
- TINS reads;
- staging writes;
- staging verification reads;
- backup copy read/write;
- publication copy read/write;
- backup/publication renames;
- stale-stage cleanup;
- successful-stage cleanup;
- numbered-backup cleanup;
- rollback removal and backup restore rename.

The render loop therefore does not synchronously wait for a 300 ms Memory Card write or a multi-second
FAT remove before drawing another wave frame.

### 3. Rollback is incremental/asynchronous

Rollback no longer calls synchronous whole-file removal for every installed item. Staging removal,
destination removal and backup restore are separate asynchronous states. If backup restore rename fails,
the backup is intentionally preserved and the diagnostic log records the failure.

### 4. Diagnostic aggregation

The 128 KiB in-RAM diagnostic ring remains. Repetitive wave stalls and async I/O latency are aggregated:

- `STUTTER_AGGREGATE`
- `ASYNC_IO_AGGREGATE`

Severe individual stalls (>= 1000 ms) are still recorded. Async I/O service latency is no longer counted
as main-EE-thread blocking time in the frame-cause classifier, preventing false wave-stutter attribution.
The PC analyzer understands both legacy per-frame logs and the new aggregates.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Output:

```text
titan-installer.elf
```

TINS v1 is unchanged. Do not rebuild `TITAN_SYSTEM.TINS` only for this Installer revision.
