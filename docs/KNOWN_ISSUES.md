# Known Issues / Release Notes

This document separates **closed release-candidate regressions** from features that are still experimental or hardware-sensitive.

## Closed on the current physical-hardware candidate

The current semi-autonomous release-candidate test cycle closed the following previously observed blockers:

- application/library scan freeze at approximately 68%;
- Package Installer transaction/persistence path used for the tested native apps;
- Project NES installation, persistence, launch, exit and relaunch;
- wLaunchELF installation/persistence;
- TitanEYE installation/persistence and runtime launch in the tested setup;
- DEV1 cold boot using uppercase `mcX:/BOOT/BOOT.ELF`;
- Welcome-screen Cross-button size/position regression;
- current 640x480 application-background path on the tested native-app flow.

These are hardware results for the tested candidate, not a promise that every possible storage device, console revision or application combination is covered.

## Autonomous Free McBoot installation

The autonomous FMCB implementation remains in source but is **not enabled in the initial public release media**.

It still requires broader physical validation across compatible console/ROM families, including MagicGate/KELF behavior, repeated cold boots, interrupted-install recovery and rollback. Do not advertise Autonomous as supported in the initial release.

## Controller / accessory coverage

Modern controller, Bluetooth and accessory support remains hardware-sensitive. Release notes should distinguish implemented support from combinations actually tested on physical hardware.

## Network / Wi-Fi coverage

Ethernet and Wi-Fi paths depend on specific adapters and network conditions. Experimental Wi-Fi support should not be described as universally compatible.

## Localization coverage

EN / PT-BR / ES coverage has been expanded, but contributors should continue routing new visible strings through the localization layer and report any literal-language leaks.

## Private ports

Any game-specific private port that depends on commercial ROM data is outside the public redistributable release. Do not publish ROMs, ISOs, BIOS data, decrypted commercial assets, or a package embedding them.

## Publication posture

The repository can be published as a **semi-autonomous release candidate / preview** once the source-hygiene, license/provenance and final export audits pass.

A later release may enable Autonomous installation after its separate hardware gate is closed.
