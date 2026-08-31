# BuildFix30 / Magenta v1 Release Gate

- BF29 FINAL baseline integrity: PASS.
- BF30 changed-path/source contracts: PASS.
- Global historical host regression command coverage: PASS (exact command-boundary segmentation; no timeout counted as PASS).
- Date/Time/Internet Time: HOST_PASS / READY_FOR_HARDWARE.
- Theme lifecycle v1-v4: HOST_PASS / READY_FOR_HARDWARE.
- Disc Service/Engine: SOURCE_COMPLETE + HOST_PASS / PHYSICAL_MEDIA_PENDING.
- Boot/TINS hardening: HOST_PASS / TARGET_BOOT_REGRESSION_PENDING.
- PUP/power: HOST_PASS / HARDWARE_PENDING.
- Store/Download/Repository v1: HOST_PASS / LIVE_TARGET_PENDING.
- SDK/API freeze: PASS.
- pikaVR/VlombisCore regressions: PASS.
- PS2DEV/R5900/IOP build: BLOCKED_EXTERNAL.
- PCSX2: BLOCKED_EXTERNAL.
- physical PS2/controller/network/storage/disc: BLOCKED_EXTERNAL.

`SOURCE_RELEASE_CANDIDATE=1` and `READY_FOR_HARDWARE=1`.

`MAGENTA_V1_READY=0` remains truthful until the required target/hardware gates are satisfied. No BF30 requirement is silently moved to BF31 and no hardware PASS is fabricated.
