# BuildFix27 Checkpoint CP41
STATUS: HOST_VALIDATED / TARGET_BINARY_BLOCKED
PREVIOUS_VALID: CP40
BLOCK: optional Titan-owned HDD/PFS IOP loader lifecycle

- Added platform-neutral dependency loader with resident-module idempotence.
- Official order fixed as dev9 -> atad -> hdd_driver -> pfs_driver.
- Kernel Makefile embeds ps2atad/ps2hdd/ps2fs only when all three official IRX materially exist.
- Builds without those bytes remain fail-closed and generate no fake headers.
- Platform verifies hdd+pfs IOMANX devices after load before PFS availability becomes true.
- Network Ethernet path now reuses resident dev9 rather than executing a duplicate copy.
- Target prerequisite validator now includes ps2atad source + binary.

NEXT: CP42 attack exact FCEUmm source materialization; if material bytes become available, integrate provider, otherwise preserve external gate and close target handoff.
