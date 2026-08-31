# Titan Installer v2.0.14 BuildFix3 — libmc IOP open flags

## Real-hardware failure fixed

`INSTALLER(4).LOG` from BuildFix2 reached entry 84/84 (`titan.elf`) and wrote the
complete staging file to `mc0:/BOOT/BOOT.ELF.TITAN.NEW`. The staging close and
verify-open also completed, but the first `mcRead()` used for staging CRC
verification returned `-5`.

PS2SDK defines `-5` as `sceMcResDeniedPermit`. The BuildFix2 telemetry also
showed the direct libmc read-open receiving newlib `O_RDONLY == 0x0`.

BuildFix3 corrects the flag-domain boundary: direct `mcOpen()` calls now use the
same explicit POSIX/newlib -> IOP `FIO_O_*` translation already used by direct
`fileXioOpen()`. This is required because libmc forwards the mode value unchanged
to MCSERV/MCMAN.

## Preserved behavior

- BuildFix2 `NEWLIB_PORT_AWARE` / `io_common.h` header ordering is unchanged.
- TINS v1 format is unchanged.
- Installer transaction/rollback design is unchanged.
- UI, renderer, waves, themes and setup flow are unchanged.
- FileXio async input/output behavior is unchanged.
- Memory Card data I/O remains asynchronous through libmc + `mcSync()`.
- Existing `BOOT.ELF` rollback-preservation behavior is unchanged.

## Build

```bash
make -C kernel clean
make -C kernel installer -j$(nproc)
```

Expected artifact: `titan-installer.elf`.

`TITAN_SYSTEM.TINS` does not need to be regenerated for this BuildFix.
