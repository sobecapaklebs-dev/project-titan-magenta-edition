# Project Titan Installer POC v2.0.9 — stale backup slot fix

## Hardware failure addressed

Physical PS2 reported:

`Não foi possível limpar um backup transacional antigo.`

v2.0.8 used `<destination>.TITAN.BAK` as a fixed transaction backup and made deletion of a stale backup mandatory before publication. On FAT/USBHDFSD a leftover directory entry can remain visible while remove/fileXioRemove fails, making an otherwise recoverable stale artifact fatal.

## v2.0.9 transaction policy

- SYSTEM/EXPLOIT transaction backups use fresh numbered names: `.TITAN.BK00` ... `.TITAN.BK63`.
- A stale `.TITAN.BAK` is never required to be deleted before publication.
- BOOT preserves the canonical `BOOT.TITAN.BAK` contract when that name is free.
- If `BOOT.TITAN.BAK` already exists, it is never overwritten; the live transaction uses a numbered backup slot.
- Rollback remembers the exact backup slot chosen for each manifest entry.
- Startup recovery scans numbered backups newest-first when a destination is missing, then falls back to legacy `.TITAN.BAK`.
- Numbered backups are removed best-effort after success. Cleanup failure cannot turn a successful install into an error.
- The v2.0.8 single-owner/main-thread bounded FileXio engine is retained unchanged. No filesystem worker thread is reintroduced.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

Only `titan-installer.elf` needs to be replaced on installation media. TINS v1 is unchanged.
