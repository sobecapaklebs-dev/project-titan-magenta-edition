# Contributing to Project Titan

Thanks for helping improve Project Titan.

## Before opening a pull request

- Keep changes focused.
- Run the relevant host tests.
- Build the affected target component when you have PS2DEV available.
- State whether the change was tested on a real PS2, emulator-only, or host-only.
- Do not include ROMs, ISOs, BIOS data, copyrighted game assets, credentials or personal logs.
- Preserve third-party licenses and notices.

## Hardware reports

A useful hardware report includes:

- PS2 model / region when relevant;
- boot method;
- connected USB/network/controller hardware;
- exact commit or build hash;
- kernel/package SHA-256;
- reproduction steps;
- trace/debug output;
- photo/video of the final visible state for a freeze.

## UI changes

New system pages should reuse the existing shell transition/focus/navigation model instead of introducing unrelated animation systems.

## Storage changes

Storage work must avoid blocking the frame-critical path. Package installation should remain transactional and incomplete staging data must never be presented as a completed install.

## Pull-request status language

Use precise wording:

- **host-validated** — host contracts passed;
- **target-built** — PS2 target compiled/linked;
- **emulator-tested** — tested only in an emulator;
- **hardware-tested** — tested on a physical PS2.

Do not use “fixed” or “release-ready” when only a host test has passed.
