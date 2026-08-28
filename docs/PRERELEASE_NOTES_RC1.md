# Project Titan: Magenta Edition — Installer Test RC1

This is the first **compiled installer test build** for community hardware testing.

## Scope

**Semi-autonomous / DEV1 only.**

```text
mcX:/BOOT/BOOT.ELF
  -> Titan Bootloader
  -> /kernel.elf on USB
  -> Project Titan
```

The experimental Autonomous / Free McBoot path is **not enabled in this release media**.

## What you can help test

Beyond installation and cold boot, reports are useful for:

- shell navigation, profiles, Settings, Library, folders and notifications;
- Package Installer;
- Titan Native applications and return-to-shell lifecycle;
- Project NES lifecycle with legal test content;
- Titan-Neutrino / Titan UL using legally obtained content;
- per-profile VMC;
- static and animated themes;
- USB storage;
- **Ethernet**;
- **experimental Wi-Fi**;
- wired and wireless controllers/accessories;
- audio continuity, UI responsiveness and long-session stability.

Wi-Fi is experimental. Include adapter chipset / VID:PID when known,
scan/authentication/DHCP/DNS results and reconnect behavior. Do not include
Wi-Fi passwords or private network dumps.

Testing guide:
https://github.com/sobecapaklebs-dev/project-titan-magenta-edition/blob/main/docs/PRERELEASE_TESTING.md

Networking notes:
https://github.com/sobecapaklebs-dev/project-titan-magenta-edition/blob/main/docs/NETWORKING.md

## Before testing

- Back up the Memory Card.
- Prefer a spare Memory Card for the first run.
- Use FAT32 USB storage.
- Extract the ZIP to the USB root.
- Launch `mass:/TITAN_INSTALL/titan-installer.elf`.

## What to report

Include PS2 SCPH model, modchip, Memory Card, USB device, display mode, exact
failing step, expected result, actual result and sanitized logs/photos.

Do not upload BIOS, commercial game data, credentials or private dumps.

## Integrity

Installer ELF SHA-256:
`1f953bf2330a7966b2f5dc2d0f752813b88d881530e6f44c2a4edb161fdb4e08`

Public TINS SHA-256:
`666bec0b8adfd7fd4099cbbb97e050b2475a7bd5c14a831d79608a537743ef93`

Release ZIP SHA-256:
`48c51da6a74a5201c3850cc0bc450845892f733e6400a55a4426d328ebcbabda`

Source commit used to build RC1:
`3e08ea3cabab9c61df219bc98b159957cc207189`

The compiled RC1 assets remain tied to that source commit even if repository
organization/documentation is improved later on `main`.
