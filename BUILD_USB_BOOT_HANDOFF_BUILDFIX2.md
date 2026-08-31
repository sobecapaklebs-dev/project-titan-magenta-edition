# Project Titan USB Boot Handoff BuildFix2

Fixes the DEV1/Matrix R1 cold-boot path specifically.

- Explicit SifExitRpc before IOP reset.
- Full IOP reboot request via SifIopReset(NULL, 0x80000000), then sync/rebind.
- Re-applies both LoadModuleBuffer and prefix-check SBV patches.
- Captures module ID and module _start result for IOMANX, FILEXIO, USBD and USBHDFSD.
- Keeps mass:/ and mass0:..mass9: kernel probing for 12 seconds.
- Replaces the obsolete "same folder" error with hardware diagnostics.

No installer transaction/UI/TINS changes.
