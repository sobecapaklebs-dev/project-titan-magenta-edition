# Project Titan Installer POC v2.0.2 — USB bootstrap hardware fix

Base: Installer POC v2.0.1 / TINS v1.

## Scope

This revision changes only the installer storage bootstrap and preflight diagnostics. UI polish, hero assets, model detection, TINS format, transaction/rollback rules, bootloader payload, themes and shell behaviour are unchanged.

## Hardware fixes

- Embedded IOMANX, FILEXIO, USBD and USBHDFSD are always attempted; the chain no longer short-circuits on one module return code.
- `SifExecModuleBuffer()` diagnostics now require both module id and `_start` result to be non-negative for a strict success result.
- `fileXioInit()` is diagnostic only; a negative result no longer aborts storage discovery before the real device probe.
- USB discovery first opens `TITAN_INSTALL/TITAN_SYSTEM.TINS` directly on the launcher alias and `mass:`/`mass0:`…`mass9:` fallbacks.
- Root `stat()` is only a secondary mount probe, used to distinguish "USB mounted, TINS missing" from "USB unavailable".
- TINS manifest loading opens the container first and validates length with the already-required seek path, rather than rejecting the file through `stat()` before opening it.
- The preflight UI reports `TITAN_SYSTEM.TINS ainda não verificado` while USB storage is unavailable instead of emitting a false second error.
- Module id/start results and `fileXioInit()` result are retained and printed for hardware diagnostics.

## Build

```bash
make -C kernel clean
make -C kernel installer
```

The resulting installer is copied to:

```text
titan-installer.elf
```

The existing `TITAN_SYSTEM.TINS` from v2.0/v2.0.1 remains compatible and does not need to be rebuilt.

## Host/static validation

```bash
python3 kernel/tests/test_installer_poc_v1.py
python3 kernel/tests/test_installer_usb_remount_v201.py
python3 kernel/tests/test_installer_usb_bootstrap_v202.py
```
