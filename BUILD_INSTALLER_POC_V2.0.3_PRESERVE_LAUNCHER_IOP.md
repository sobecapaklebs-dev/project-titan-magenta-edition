# Installer POC v2.0.3 — preserve launcher IOP/USB

This revision changes the physical-hardware startup strategy only.

If the installer can rebind FILEXIO and open `TITAN_INSTALL/TITAN_SYSTEM.TINS` before resetting the IOP, it preserves the launcher's IOP image and binds directly to the resident PADMAN service. This keeps the USB stack that already proved it can read the installer on the real console.

Only when inherited USB is unavailable (or inherited PAD binding fails) does the installer fall back to the previous clean IOP reset + embedded USB bootstrap.

No installer UI, TINS format, transaction, bootloader, theme, renderer, or installation payload behavior was changed.
