# Project Titan Installer POC v2.0.8 — single-owner fast stream

## Hardware symptom fixed

v2.0.7 could enter `INSTALLING` and remain at 1% with the Magenta waves still
animating but with no USB activity. The I/O job itself was moved to a second EE
thread at priority 96.

This violated an already-documented Project Titan hardware rule in
`kernel/src/services/titan_storage_scheduler.c`: real PS2 package installation
regressed to 0% when the FileXio/newlib path was moved to a second EE thread.
Package/update I/O therefore uses the UI/main EE thread as the single filesystem
RPC owner.

The installer now follows the same rule.

## v2.0.8 changes

- removes the second EE installer filesystem worker;
- all installer FileXio/newlib operations remain on the main EE thread;
- uses an adaptive bounded pump per rendered frame;
- 32 KiB maximum per individual read/write RPC;
- up to 64 KiB of accounted I/O work per rendered frame;
- 10 ms soft time budget plus 24 metadata-state transitions per frame;
- keeps input, waves and rendering between bounded filesystem quanta;
- publishes each validated entry immediately instead of staging the entire
  installation and only discovering publication errors at the end;
- keeps `.TITAN.BAK` files until transaction commit, preserving rollback;
- POSIX `rename()` + `fileXioRename()` remain the fast publication path;
- if rename is unavailable, backup/publication copy fallback is itself split
  into bounded 32 KiB state-machine reads/writes, so large files cannot hide a
  whole-file synchronous copy inside one UI frame;
- live rollback removes partial streamed destinations before restoring backup.

## Throughput rationale

v2.0.6 advanced only one 8 KiB state per frame. Since source read and staging
write are separate states, a large file could be effectively throttled to one
8 KiB payload quantum every two rendered frames before verification/backup
passes were counted.

v2.0.8 uses a 32 KiB RPC quantum and an adaptive pump. The frame budget is high
enough that the USB 1.1/storage stack should become the limiting factor rather
than an artificial 8 KiB/frame gate, while each individual blocking operation
remains bounded so the shell gets control back regularly.

## Transaction order

For each selected entry:

1. sequential TINS read + source CRC;
2. bounded staging write;
3. full staging CRC only for critical/small files;
4. preserve previous destination (`rename`, `fileXioRename`, bounded-copy fallback);
5. publish this file immediately (`rename`, `fileXioRename`, bounded-copy fallback);
6. continue to the next entry;
7. after all entries: write `INSTALL.COMPLETE`;
8. cleanup non-BOOT backups;
9. mark success/self-invalidate.

This means a filesystem publication incompatibility is discovered on the first
affected file, not after staging the whole installation.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Output:

```text
titan-installer.elf
```

The TINS format and payload do not change in v2.0.8.
