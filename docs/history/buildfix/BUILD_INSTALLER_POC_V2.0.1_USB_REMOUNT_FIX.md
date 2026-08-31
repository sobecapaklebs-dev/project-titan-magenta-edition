# Project Titan Installer POC v2.0.1 — USB remount hardware fix

This revision fixes the first physical-PS2 TINS preflight failure observed after launching the installer from wLaunchELF.

## Hardware symptom

The installer itself launched from USB, but the preflight page reported both:

- `ERRO Armazenamento USB`
- `ERRO TITAN_SYSTEM.TINS inválido ou ausente`

Both Memory Cards were visible.

## Root cause

`TitanInput_Initialize()` initializes `TitanIopServiceManager`, which intentionally performs a clean IOP reset. That reset removes the USB stack and the `mass:` mount inherited from wLaunchELF. The installer then reloaded IOMANX/FileXio/USBD/USBHDFSD but waited only 350 ms and tested `mass:/` once. Physical flash drives can require substantially longer to enumerate and may reappear through a `massN:` alias.

## Fix

- replace the fixed 350 ms delay with bounded polling: 120 attempts × 100 ms (maximum ~12 s);
- prefer the `mass:`/`massN:` prefix used by `argv[0]` when the installer was launched;
- fall back across `mass:` and `mass0:` … `mass9:`;
- identify the correct device by the presence of `TITAN_INSTALL/`, not merely by the existence of a generic mass root;
- store the resolved USB prefix in installer state;
- open `TITAN_SYSTEM.TINS`, write SYSTEM entries, `INSTALL.COMPLETE`, and `INSTALLER.CONSUMED` through that resolved prefix;
- delay the one-shot consumed-marker check until after USB has been remounted;
- make parent-directory handling safe for `massN:/` prefixes;
- preserve all TINS CRC, staging, rollback, Memory Card boot backup, UI, 720p, model detection, and installer-polish behavior from v2.0.

## Host validation

- `python3 kernel/tests/test_installer_poc_v1.py`
- `python3 kernel/tests/test_installer_usb_remount_v201.py`

Both must return OK. A real PS2 test is still required because USB enumeration timing is hardware-dependent.
