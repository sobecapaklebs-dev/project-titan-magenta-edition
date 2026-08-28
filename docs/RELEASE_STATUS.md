# Release Status — Initial Public Candidate

## Supported installation method

**Semi-autonomous / DEV1 is the supported installation method for the initial public release.**

```text
mcX:/BOOT/BOOT.ELF
  -> Titan Bootloader
  -> USB /kernel.elf
  -> Project Titan
```

The uppercase filename is part of the boot contract.

## Hardware-validated candidate gates

The current candidate has been physically exercised for:

- semi-autonomous installer completion;
- DEV1 cold boot into `BOOT.ELF`;
- bootloader -> USB kernel -> shell;
- application/library scanning past the previous 68% freeze;
- Package Installer commit/persistence in the tested flow;
- wLaunchELF installation/persistence;
- Project NES installation/persistence/launch/exit/relaunch;
- TitanEYE installation/persistence/runtime launch in the tested setup;
- current Welcome Cross-button presentation.

These results apply to the tested candidate and hardware setup. They do not replace per-device compatibility testing for every PS2 model, controller, network adapter, title or display mode.

## Autonomous installation

The integrated Free McBoot implementation remains part of the source tree for future development, but:

- it is experimental;
- it is disabled from the initial public release media;
- it must not be presented as a supported install option in this release;
- its future enablement requires a separate hardware-validation matrix.

## Publication gate

Before the first public commit/tag:

1. freeze the exact tested source tree;
2. run the pre-publication source and secret/proprietary-content audit;
3. verify third-party notices and redistribution rights;
4. generate a clean public export;
5. record SHA-256 for the frozen source snapshot;
6. publish the exact audited tree, then tag only that commit.

Do not substitute an older pre-hardware archive for the physically tested source tree.
