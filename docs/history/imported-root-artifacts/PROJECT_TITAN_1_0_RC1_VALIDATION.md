# Project Titan: Magenta Edition — 1.0 RC1 Validation Report

Date: 2026-08-22
Status: **HOST VALIDATED / PS2 TARGET BUILD PENDING / HARDWARE ACCEPTANCE PENDING**

## Result

The BF30 hardware-feedback correction and full VlombisCore materialization are host-closed. No known functional host gate remains red in the final source state.

The canonical runner was executed in segments because the complete historical suite exceeds a single execution window. Segment boundaries were resumed from the last completed test rather than restarting or skipping failed gates.

## Final RC-specific gates

- `test_bf30_hwfix2_performance_regression_contract.py`: **PASS** — optimized 720p default, single final fence, restored Snake/VU1 Flow.
- `test_bf30_hwfix2_input_bt_welcome_contract.py`: **PASS 9/9**.
- `test_bf30_hwfix2_perf_tech_observability_contract.py`: **PASS 8/8**.
- `test_bf30_vlombis_full_integration_contract.py`: **PASS 16/16**.
- `test_release_contract.py`: **PASS** after the release contract was updated to recognize bounded adaptive package quanta while still requiring worker parking/single-owner FileXio.
- `test_source_hardening_contract.py`: **PASS** with the same single-owner requirement.
- `test_buildfix30_localization_contract.py`: **PASS** after its fixed System-row count was updated from 14 to 16 for the new pikaVR/Vlombis rows.
- `test_buildfix30_target_source_audit_contract.py`: **PASS**.

## Major subsystem coverage observed PASS

- Profile/VMC/storage fallback
- Neutrino/UL
- Application discovery
- Package Installer transaction
- TPKG
- Unified Input
- DualSense
- Xbox USB/BT/BLE
- Early Input
- keyboard/mouse
- Engine overlays/return
- Fast Boot Library catalog + incremental metadata reuse
- HOME/folders/shell/DEX/video persistence
- Theme service + XMB spline
- 480i/720p renderer
- VU1 Flow/Snake
- audio runtime
- PUP/update/rollback
- wLaunchELF
- Project NES/FCEUmm
- Titan app/network/VFS/PFS lifecycle
- Theme v4 lifecycle/degradation/profiler
- Repository/Download/Store
- Date/Time/Internet Time
- Disc engine/session/return/Smart IGR
- Localization architecture
- Boot/TINS packaging
- API v1 freeze
- FMCB autonomous install/rollback/boot chain

## Source-clean audit — inherited exception

`test_buildfix29_open_source_readiness_contract.py` is not a functional runtime gate for this inherited hardware/build tree. It reports pre-existing compiled/distribution payloads.

Measured comparison:

- original user-supplied BF30 tree: return code 2, **80 `ERROR` records**;
- 1.0 RC1 working tree: **80 `ERROR` records** at the same gate.

The failures include inherited `kernel.elf`, stage2 ELFs, dist payloads, Neutrino modules, PADEMU/AR9271 target outputs, FMCB distribution copies and third-party sample material. The VU1 allowlist hash mismatch is also present on the baseline binary supplied with BF30.

This exception must still be cleaned for a future public source-only release; it is not evidence of an RC1 functional regression.

## Resolved during validation

Two tests initially failed because their assertions encoded superseded architecture rather than a real runtime fault:

1. Package ownership contracts expected the literal fixed call `TitanPackageInstaller_Update(TITAN_STORAGE_PACKAGE_BUDGET)`. RC1 legitimately uses a bounded Vlombis-selected `package_budget`. The contracts now accept the adaptive form only when worker parking, min/max bounds and single-owner scheduling are all still provable.
2. Localization architecture expected a fixed 14-row System page. RC1 intentionally adds pikaVR and VlombisCore, making the count 16. The test now verifies the new technology rows explicitly.

Both corrected contracts pass in the final state.

## Target boundary

No `mips64r5900el-ps2-elf-gcc`, `mips64r5900el-ps2-elf-ld` or IOP target compiler is installed in this execution environment. Therefore target link success and real PS2 performance are not claimed by this report.

Existing target ELFs/IRXs in the inherited source tree must be considered stale until the user rebuilds this RC1 source with the PS2DEV/PS2SDK environment that successfully built BF30.

## Promotion rule

Do not update frozen final version metadata to 1.0 solely on this host report. Rebuild the target and complete `PROJECT_TITAN_1_0_RC1_HARDWARE_GATE.md`. If that physical gate passes, this source lineage is the proposed Project Titan: Magenta Edition 1.0 baseline.
