# Project Titan Installer POC v2.0.10 — Robust Diagnostics

This revision adds hardware diagnostics without changing the Installer UI.

## Log files

Primary:

`mass:/TITAN_INSTALL/INSTALLER.LOG`

Previous run:

`mass:/TITAN_INSTALL/INSTALLER.PREV.LOG`

If the USB log cannot be persisted at the end of the run, the selected Memory
Card is used as a fallback:

`mcN:/TITAN/INSTALLER.LOG`

## Non-perturbing logging policy

During the active transaction, diagnostic events are written to a 128 KiB
circular buffer in EE RAM. The installer does not append a log line to USB for
every read/write block. This is intentional: logging to the same USB device
would change the timing under test and could create false wave stutters.

A pre-install checkpoint is written before the transaction starts. The full RAM
trace is flushed after success/failure. The previous USB log is copied to
`INSTALLER.PREV.LOG` when a new session binds to storage.

## Wave-stutter diagnostics

Every rendered frame records:

- total frame duration;
- input update duration;
- install-pump duration;
- render + VSync duration;
- current install phase and path;
- number of filesystem operations in that frame;
- total filesystem time;
- slowest filesystem call, result and errno.

A `WAVE_STUTTER` event is emitted when a frame exceeds the display target by
more than 5 ms. It carries one of these diagnoses:

- `LIKELY_MISSED_VSYNC_AFTER_IO`
- `SYNC_IO_BLOCKED_MAIN_EE_THREAD`
- `SLOW_FILESYSTEM_RPC`
- `CRC_OR_CPU_WORK_BLOCKED_MAIN_EE_THREAD`
- `INSTALL_METADATA_OVERRAN_BUDGET`
- `RENDER_OR_VSYNC_WAIT`
- `INPUT_OR_IOP_SERVICE_STALL`
- `UNATTRIBUTED_MAIN_THREAD_GAP`

The log also states `wave_effect=no_frame_rendered_for_Xms`. This is important
because the Magenta installer background currently advances from the shell
frame counter: when the main EE thread does not reach the renderer, the wave
cannot advance visually during that interval.

## Transaction diagnostics

The log records TINS preflight, entry/phase changes, backup-slot allocation,
rename/remove fallbacks, source/staging CRC mismatches, publication method,
rollback entry point, POSIX errno, FileXio return values and progress/work
counters.

Every five seconds a `PERF_WINDOW` line summarizes FPS, stutter count, USB I/O
throughput, average install-pump/render times, worst frame and worst I/O call.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Only `titan-installer.elf` changes. `TITAN_SYSTEM.TINS` does not need to be
rebuilt for this diagnostic revision.
