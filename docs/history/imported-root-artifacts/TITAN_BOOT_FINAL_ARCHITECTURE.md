# Titan Boot Final Architecture — Magenta Edition v1 / BF30

## Frozen physical baseline
BF30 does not replace the known-working USB BDM Core BuildFix6 / wLaunchELF-order bootloader. The validated chain remains DEV1/Matrix-Modbo shortcut -> Titan bootloader on Memory Card -> resident IOP/USB BDM bootstrap -> Titan system payload. The corresponding source ordering keeps IOMANX/FileXio, SIO2MAN/MCMAN/MCSERV/PADMAN and USB BDM ownership aligned with the hardware-proven path.

## Stage naming
BF30 does not invent a new Stage-0/TBOOT/TSYS chain. In release documentation, "Stage-0" means the already-approved external console entry mechanism (for example DEV1); "Titan bootloader" means the existing MC bootloader; and "system payload" means the existing Titan kernel/system files delivered by TINS. TBOOT/TSYS are reserved names only until a separately proven architecture assigns them an on-media format.

## Failure policy
Bootloader ELF validation, bounded IOP/USB probing and persistent fatal diagnostics remain intact. A missing/invalid system package is not executed. Recovery remains a separate reachable path; release hardening must never overwrite the last known-good boot route merely to publish a new package.

Target rebuild and hardware regression remain external gates.
