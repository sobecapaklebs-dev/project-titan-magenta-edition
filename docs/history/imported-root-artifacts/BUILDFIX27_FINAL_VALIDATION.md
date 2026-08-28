# BuildFix27 Final Validation

Date: 2026-08-20
Result: **SOURCE COMPLETE / HOST PASS / TARGET EXTERNAL VALIDATION PENDING**

## Final material recovery
The historical CP49 recovery dependency on a non-persisted CP14 full archive was corrected. Final recovery uses only persisted material:

`CP00 full -> CP10R -> CP11..CP49 -> FCEUmm real integration -> final closure delta`

The final recovery tool is `scripts/rehydrate_buildfix27.py`; it validates locked archive hashes, safe tar extraction, removes known generated host artifacts, and requires final manifest equality with zero missing, unexpected, or mismatched hashes.

## FCEUmm / Project NES
PASS:
- frozen commit/tree provenance and imported SHA-256 manifest;
- real FCEUmm source present (687 upstream files);
- Project NES provider/conformance/storage/save/lifecycle contracts;
- libretro adapter contract;
- `make -C apps/project-nes host-test`;
- adapter ASan+UBSan;
- GPL/provenance package gate;
- Titan video/audio/input/save/reset/load/unload/lifecycle/exit integration contracts.

Target build selects portable FCEUmm C sources with `STATIC_LINKING=1`, `HAVE_HDPACK=0`, `HAVE_NTSC=0`; no ARM/ARM64/NEON compiler path is selected for PS2.

## Complete BF27 host regression
`kernel/tests/run_host_tests.sh` was executed in exact consecutive script segments because the execution wrapper has a per-call time limit. Timeouts were never counted as PASS. All script ranges completed successfully after correcting stale final-status expectations in three BF27 closure contracts and updating the rehydration contract for the persisted CP00 chain.

Coverage includes baseline storage/VMC/Neutrino/UL/package/runtime/UI/renderer/audio/network/updater, Application Runtime/SDK, native apps, SAS, Browser/network, wLaunchELF, Project NES/FCEUmm, PFS, CP47 target orchestrator, CP48 evidence tooling, and final rehydration.

Sanitizer-bearing contracts retained ASan+UBSan; tests were not disabled or bypassed.

## Source/build audit
- executor-specific build paths `/home/runner`, `/workspace`, `/mnt/data`: none in executable source/build paths after closure;
- `host-only` remaining outside test paths: pinned TLS dependency preparation tool, intentionally a host build-preparation utility;
- UI `placeholder` occurrences are text/input/default-art placeholders or historical DEX APP_HOME UI, not Project NES provider/core implementation;
- vendor FCEUmm TODO/HACK markers, if any, remain upstream provenance and are not silently rewritten;
- no commercial ROM included.

## Target probe in this executor
- `mips64r5900el-ps2-elf-gcc`: NOT_FOUND
- `mips64r5900el-ps2-elf-g++`: NOT_FOUND
- `mips64r5900el-ps2-elf-ar`: NOT_FOUND
- `mips64r5900el-ps2-elf-ld`: NOT_FOUND
- `dvp-as`: NOT_FOUND
- `PS2DEV`: UNSET
- `PS2SDK`: UNSET
- PCSX2: NOT_FOUND
- `ps2atad.irx`, `ps2hdd.irx`, `ps2fs.irx`: NOT_MATERIALIZED

Therefore TARGET_BUILD_VALIDATED=0, PCSX2_VALIDATED=0 and HARDWARE_VALIDATED=0 without affecting SOURCE_COMPLETE/HOST_VALIDATED.

## Final clean rehydration result
Using only the persisted-material set (CP00 full, CP10R, CP11..CP49, real FCEUmm integration, final closure delta) plus the final manifest:

```text
EXPECTED=4272
ACTUAL=4272
MISSING=0
UNEXPECTED=0
HASH_MISMATCH=0
BUILDFIX27_FINAL_REHYDRATE_OK
```

This is the source-freeze identity gate.

**BUILDFIX27 SOURCE FREEZE COMPLETE.**
