# Project Titan Installer POC v2.0.6 — Cooperative Streaming Installation

This revision keeps the v2.0.5 visual/UI contract and changes the installation execution model.

## Hardware issue addressed

The v2.0.5 installation path executed recovery, complete-file TINS reads, staging writes,
staging re-reads/CRC and publication synchronously inside one frame-loop iteration. On a
real PS2 USB 1.1 path this could leave the UI/waves frozen for a long time while the USB LED
continued to blink, and progress could remain at 0% until a whole operation completed.

## v2.0.6 architecture

Installation is now a cooperative state machine. The main UI loop advances one bounded
storage step per iteration and always returns to input/render between data-transfer steps.

Data transfer uses 8 KiB I/O slices:

- sequential TINS read (seekless)
- staging write to `.TITAN.NEW`
- staged-file CRC re-read
- byte-granular progress accounting

Recovery, publication, backup cleanup and rollback also advance incrementally by manifest
entry instead of sweeping the complete manifest before rendering again.

Transactional safety is retained:

- source CRC while streaming from TINS
- `.TITAN.NEW` staging
- staged-file size + CRC verification
- `.TITAN.BAK` before publication
- rollback on failure/cancel
- persistent DEV1 `BOOT.TITAN.BAK`
- `INSTALL.COMPLETE` and installer consumed marker

## Visual contract

No layout, background, wave, color, controller artwork, terms layout or page geometry was
changed by v2.0.6. The existing Magenta background keeps rendering during installation.

## Build

From the project root:

    make -C kernel clean
    make -C kernel installer

Output:

    titan-installer.elf

The existing `TITAN_SYSTEM.TINS` does not need to be rebuilt for this revision.
