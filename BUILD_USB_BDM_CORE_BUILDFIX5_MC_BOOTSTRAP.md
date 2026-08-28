# Project Titan USB BDM Core BuildFix5 — MC bootstrap isolation

Base: USB BDM Core BuildFix4.

Scope: bootloader MC/logger bootstrap only. Kernel BDM runtime from BF4 is unchanged.

Changes:
- Embed PS2SDK mcman.irx and mcserv.irx instead of relying on rom0:MCMAN/MCSERV for the diagnostic logger.
- Load rom0:SIO2MAN before embedded MCMAN/MCSERV.
- Memory-card logging is non-critical: failure never aborts USB bootstrap.
- Add visual checkpoints: 16 IOMANX, 18 FILEXIO, 20 SIO2MAN, 22 MCMAN, 24 MCSERV, 26 mcInit, 30 USBD, 35 BDM, 40 FATFS, 45 USBMASS_BD, 48 FileXio, 50 USB stack done.
- Error screen includes MC bootstrap diagnostics.
- USB BDM chain and kernel runtime remain BF4 behavior.
