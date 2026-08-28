# Community testing — Installer Test RC1

## Installation boundary

Supported public path:

```text
Matrix / Modbo DEV1
  -> mcX:/BOOT/BOOT.ELF
  -> Titan Bootloader
  -> /kernel.elf on USB
  -> Project Titan
```

The Autonomous / Free McBoot path remains experimental and is **not enabled in
the RC1 release media**.

## What to test

After successful installation and cold boot, useful community coverage includes:

- Welcome screen and HOME navigation;
- profiles, Settings, notifications and folders;
- Library scanning;
- Package Installer transactions;
- Titan Native applications and return-to-shell lifecycle;
- Project NES lifecycle using legal test content;
- Titan-Neutrino / Titan UL paths using legally obtained content;
- per-profile VMC behavior;
- static and animated themes;
- USB storage;
- Ethernet;
- experimental Wi-Fi;
- wired and wireless accessory/controller paths;
- audio continuity, UI responsiveness and long-session stability.

See [Networking](NETWORKING.md) for network-report details.

## What to report

Include PS2 SCPH model, modchip/configuration, Memory Card type, USB storage
device, display mode, exact reproduction steps, expected result, actual result,
and sanitized Titan logs/photos when available.

Do not upload BIOS dumps, commercial game data, credentials, Wi-Fi passwords
or private device dumps.

A feature being present in source or listed for community testing does not mean
that every controller, Wi-Fi adapter, network environment, game, display mode
or PS2 revision has been exhaustively hardware-validated.
