# Project Titan 0.3.90-rc14 — USB Boot Handoff BuildFix1

Base: Installer v2.0.14 BuildFix6 source (successful physical installation).

## Physical symptom

- Semi-autonomous DEV1 bootloader installed at `mc0:/BOOT/BOOT.ELF` starts, but cannot find `kernel.elf` on USB.
- When the kernel is launched by another path, its runtime storage layer does not reliably rediscover the USB device after IOP reset.

## Root cause

The installed bootloader assumed that a launcher had already left IOMANX/FileXio/USBD/USBHDFSD resident. That assumption is false when BOOT.ELF starts directly from Memory Card DEV1: it called `SifInitRpc()` and immediately probed `mass:/kernel.elf` without creating a USB stack.

The kernel then performs its own clean IOP reset. Its USB bootstrap already reloads the embedded modules, but mount discovery allowed only about four seconds and the common storage boundary translated only `mass:` -> `mass0:`. Physical installer tests already proved that re-enumeration can require substantially longer and can appear as a numbered mass alias.

## BuildFix1

### Bootloader

- Owns a clean bounded IOP reset/sync sequence when started from MC.
- Embeds and attempts IOMANX, FileXio, USBD and USBHDFSD in order.
- Initializes the EE FileXio client but treats the actual filesystem probe as authoritative.
- Polls for USB for up to about 12 seconds.
- Searches root `kernel.elf` on `mass:` and `mass0:`...`mass9:` plus the existing fallback layouts.
- Keeps stage2/ELF validation and visible boot flow unchanged.

### Kernel

- Keeps the existing post-reset embedded USB bootstrap.
- Extends mount grace from ~4 s to ~12 s.
- Generalizes the storage boundary from only `mass0:` fallback to `mass0:`...`mass9:` for open/stat/mkdir/remove/rename/list operations.
- Canonical on-disk paths remain `mass:/...`; only the I/O boundary resolves aliases.

## Fast physical test without reinstalling

Build:

```bash
make -C bootloader clean
make -C bootloader -j$(nproc)
make -C kernel clean
make -C kernel -j$(nproc)
```

Then:

1. Copy the new root `kernel.elf` to the root of the Titan USB device.
2. Using wLaunchELF or another known-good path, replace `mc0:/BOOT/BOOT.ELF` with `bootloader/titan.elf`.
3. Cold boot through the configured DEV1 shortcut.

The complete installer does not need to be rerun for this hardware test.

For future installer media, rebuild `TITAN_SYSTEM.TINS`, because both BOOT and SYSTEM payloads changed.
