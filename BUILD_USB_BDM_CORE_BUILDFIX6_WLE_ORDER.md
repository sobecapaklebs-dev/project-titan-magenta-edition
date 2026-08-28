# Project Titan USB BDM Core BuildFix6 — wLaunchELF pre-USB ordering

Base: BuildFix5 MC bootstrap.

Scope: USB bootstrap ordering in bootloader + kernel only.

- Bootloader pins PS2SDK `mcman-old.irx` / `mcserv-old.irx`, matching wLaunchELF.
- Adds ROM PADMAN to the basic-module set.
- Moves `fileXioInit()` and 128 KiB FileXio RW buffer setup before USBD.
- `mcInit()` follows FileXio setup, before USBD.
- Kernel binds its FileXio client after embedded FILEXIO and before USBD/BDM.
- BDM chain, USB aliases, installer BF6 transaction and UI are unchanged.

Physical target: BF5 cold DEV1/R1 freeze at the 30% USBD checkpoint.
