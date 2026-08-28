# Project Titan USB Boot Handoff BuildFix3 — Memory Card boot log

Base: USB Boot Handoff BuildFix2.

Scope: bootloader diagnostics only. Kernel and installer are unchanged.

## Persistent boot log

The bootloader keeps diagnostics in a 16 KiB EE-RAM buffer from the first boot steps. After the IOP reset, it loads `rom0:MCMAN` + `rom0:MCSERV`, binds libmc, and flushes the buffered trace to the Memory Card.

Default path:

`mc0:/TITAN_BOOTLOADER.LOG`

If `argv[0]` explicitly starts with `mc1:`, the logger uses slot 1 instead.

The log records bootloader version/argv, IOP reset and sync, LoadFile/SBV results, MCMAN/MCSERV/mcInit, embedded IOMANX/FILEXIO/USBD/USBHDFSD module IDs and `_start` results, fileXioInit, kernel candidates, periodic USB probe attempts, ELF validation results, selected kernel, stage2 relocation and final handoff/fatal stage.

The file is rewritten from the in-memory buffer on each major checkpoint instead of relying on USB or append support. This makes the trace available even when `mass:` never enumerates.

## Build

```bash
make -C bootloader clean
make -C bootloader -j$(nproc)
```

Copy `bootloader/titan.elf` to `mc0:/BOOT/BOOT.ELF`, cold boot through the Matrix/Modbo R1 path, then inspect `mc0:/TITAN_BOOTLOADER.LOG` with wLaunchELF.
