# BuildFix28 Final Validation

Date: 2026-08-20
Baseline: BuildFix27 final target-ready source with real frozen FCEUmm.

## Result

- BUILDFIX28_SOURCE_IMPLEMENTATION_COMPLETE=1
- GLOBAL_HOST_REGRESSION_PASS=1
- FINAL_CLEAN_TREE_HOST_REGRESSION_PASS=1
- TARGET_BUILD_VALIDATED=0 — EXTERNAL_PS2DEV_PENDING
- PCSX2_VALIDATED=0 — EXTERNAL_PENDING
- HARDWARE_VALIDATED=0 — EXTERNAL_PENDING

## Final clean-tree host regression

The official `kernel/tests/run_host_tests.sh` was executed against the clean BF28 final staging tree. Executor wall-clock limits required continuation at exact script boundaries only; a timeout was never counted as PASS. The continuation reached the final `buildfix28-lifecycle-recovery-contract: OK` gate.

Coverage includes historical non-regression plus BF28 Session Service, Native/Project NES/UL/Neutrino Smart IGR and Overlay Resume/Return, Unified Input, DualSense USB/Bluetooth, Xbox One/Series USB GIP and Bluetooth/BLE, asynchronous Early Input, keyboard/mouse, platform fast paths/overhead, pikaVR/VlombisCore pre-freeze, Theme v1-v4, flagship scene-v4, shutdown and recovery.

ASan/UBSan remained enabled in sanitizer-bearing contracts defined by the official runner.

## Source audit

- No BF28 production TODO/FIXME/NOT_IMPLEMENTED/STUB/PLACEHOLDER/MOCK/fake-backend occurrence was found in the audited new runtime/service/app/tool paths.
- No executor-specific absolute path is used by executable BF28 source/build logic. `/mnt/data` occurrences that remain in historical patch/evidence text are provenance-only and not build dependencies.
- Generated host build/cache/object artifacts were removed before final packaging.

## External validation truth

The current executor does not contain PS2DEV/PS2SDK, the R5900 target toolchain, PCSX2 or physical PS2/controller hardware. No target, emulator or hardware PASS is inferred from host validation.
