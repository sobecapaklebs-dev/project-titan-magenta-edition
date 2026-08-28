# Project Titan USB BDM Core BuildFix4

Scope: bootloader + runtime kernel USB bootstrap only. Installer transaction remains BuildFix6.

## Bootloader
- Reset sequence aligned with wLaunchELF: SifInitRpc -> SifIopReset("",0) -> SifIopSync -> RPC/loadfile -> SBV patches.
- Runtime USB stack changed from USBD+USBHDFSD to USBD+BDM+BDMFS_FATFS+USBMASS_BD.
- MC diagnostic logger retained.
- mass:/ and mass0:..mass9: kernel probing retained.

## Kernel
- Runtime storage stack changed to USBD+BDM+BDMFS_FATFS+USBMASS_BD.
- USBHDFSD remains embedded only because the existing installer source still references it; runtime no longer mounts through it.
- Startup waits up to 12 seconds for physical USB enumeration and uses existing mass0..mass9 translation.

## Build
make -C bootloader clean && make -C bootloader -j$(nproc)
make -C kernel clean && make -C kernel -j$(nproc)

After hardware validation, rebuild TITAN_SYSTEM.TINS so BOOT and SYSTEM payloads contain these binaries.
