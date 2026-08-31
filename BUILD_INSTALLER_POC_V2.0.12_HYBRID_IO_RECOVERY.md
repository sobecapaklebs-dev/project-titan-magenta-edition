# Project Titan Installer POC v2.0.12 — Hybrid I/O + Recovery Fix

This revision is based on the second real-hardware diagnostic log from Installer v2.0.11.

## Hardware findings addressed

1. v2.0.11 successfully opened `TITAN_SYSTEM.TINS` through direct FileXio NOWAIT, but the first async data read completed as a zero-byte/invalid read and the transaction aborted at entry 1/84 with `Falha ao posicionar o stream do TINS.`
2. Recovery still probed `.TITAN.BK63 ... BK00` for every manifest entry. On the physical PS2 this produced 67 blocking `stat()` calls in a single frame and flooded the diagnostic ring before payload installation began.
3. Direct FileXio metadata calls against `mc0:` returned `-2` for stale BOOT staging cleanup. Memory Card operations need to use libmc rather than FileXio.
4. The old v2.0.10 log showed large synchronous Memory Card writes blocking the wave renderer for hundreds of milliseconds per 32 KiB block.

## v2.0.12 architecture

### `mass:` data plane

The TINS stream and USB destination files use the known-working POSIX/newlib path (`open/read/write/close`). Reads/writes are latency-adaptive micro-slices instead of fixed 32 KiB foreground calls.

Initial targets:

- USB read: 4 KiB
- USB write: 4 KiB
- target blocking call: ~8 ms

The slice halves when a call is materially over budget and grows when calls are comfortably below budget. `IO_SLICE_TUNE` records changes in `INSTALLER.LOG`.

### Memory Card data + metadata plane

`mc0:`/`mc1:` transactions use libmc's native asynchronous operations:

- `mcOpen`
- `mcRead`
- `mcWrite`
- `mcClose`
- `mcDelete`
- `mcRename`
- `mcSync(MC_NOWAIT)` polling between rendered frames

This keeps BOOT staging/verification from waiting synchronously on the EE render loop. If `mcRename` is unavailable on the active ROM module, the existing transaction state machine falls back to incremental copy + delete.

### `mass:` metadata plane

Potentially long FAT metadata operations keep the v2.0.11 FileXio NOWAIT path:

- `fileXioRemove`
- `fileXioRename`
- `fileXioWaitAsync(FXIO_NOWAIT)`

Only one filesystem RPC is outstanding at a time.

## Recovery fix

Legacy recovery no longer scans all 64 numbered `.TITAN.BKxx` names for every destination. Numbered backups from old unjournaled runs are preserved rather than guessed. The only legacy backup with stable semantics is `.TITAN.BAK`, which remains recoverable.

Live v2.0.12 rollback does not need a scan: the exact backup slot selected for the current transaction remains stored in its manifest entry.

USB backup allocation is bounded to 8 candidate probes instead of 64. BOOT allocation still performs one real `/BOOT/*` enumeration with `mcGetDir`.

## Diagnostic noise reduction

Expected missing `.TITAN.NEW`, `.TITAN.BKxx` and `.TITAN.BAK` probes during RECOVER/ROLLBACK/PREPARE_STAGE/PUBLISH_ENTRY no longer emit one `IO_FAILURE` line each. They are still counted in performance metrics.

## Important invariants retained

- TINS v1 format unchanged.
- TINS source CRC verification retained.
- Full staging CRC retained for critical/small payloads.
- Transactional `.TITAN.NEW` publication retained.
- Existing BOOT backup contract retained.
- Rollback/recovery retained.
- Installer UI, waves, themes, transitions and visual layout are unchanged.
- No second EE filesystem worker thread.

## Build

From the Project Titan source root:

```bash
make -C kernel clean
make -C kernel installer
```

Output:

```text
titan-installer.elf
```

Replace only `TITAN_INSTALL/titan-installer.elf` on the installer USB. `TITAN_SYSTEM.TINS` does not need to be regenerated.
