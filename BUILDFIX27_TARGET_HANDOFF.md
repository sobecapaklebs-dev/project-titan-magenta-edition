# BuildFix27 Target / External Handoff — CP34

## Target toolchain
CP21 reprobe: PS2DEV and PS2SDK unset; mips64r5900el-ps2-elf-gcc, ld, objdump and dvp-as not found. Do not claim R5900/VU build until these are available.

## Required target sequence
1. Reconstruct the latest persisted BF27 checkpoint from the delta chain.
2. Build the kernel and native apps with the supported PS2DEV/R5900 toolchain.
3. Build the frozen Fceumm-PS2 reference unchanged before integrating a provider.
4. Run PCSX2 smoke/lifecycle tests.
5. On physical PS2: cold boot Reference 720p, capture memory watermarks, run 10-cycle shell/theme/TitanEYE/native-app lifecycle, then active Network/Storage/FTP/VMC tests.
6. Only after target evidence, promote any target-dependent feature/status.

## Known external blockers
- FCEUmm source bytes are not materialized in the local build workspace.
- Titan-owned PFS lifecycle/provider/hooks/lazy loader exist, but target IRX binaries `ps2atad.irx`, `ps2hdd.irx`, and `ps2fs.irx` are not materialized in this workspace; no target writable PFS mount is claimed.
- No target toolchain/PCSX2/hardware execution is available in this environment.

## HDD/PFS target prerequisites (CP40)
- Official PS2SDK ref audited: `a13b5971ec0e39c7ba8b8559b80a4e81c8425352`.
- Required IOP outputs: `$(PS2SDK)/iop/irx/ps2atad.irx`, `ps2hdd.irx` and `ps2fs.irx`.
- Run `python3 scripts/validate_pfs_target_prereqs.py --require-ready` before enabling target PFS validation.
- Load the official drivers only through Titan-owned IOP lifecycle; apps never load IRX or receive FileXio ownership.

- CP41 lazy loader uses resident identities `dev9`, `atad`, `hdd_driver`, `pfs_driver`; it skips modules already resident and verifies `hdd`+`pfs` IOMANX devices after loading.

## FCEUmm material import (CP42)
Use only `scripts/import_fceumm_sources.py` for incoming archives. Verify the modern and PS2 frozen profiles before extraction; do not substitute a branch head or newer commit. After import, retain GPL-2.0 Copying/source obligations before enabling a linked provider.

## Deterministic PS2DEV intake
Run `scripts/validate_ps2dev_bundle.py <bundle-root> --require-ready --emit-env <path>/bf27-target-env.sh`. Source the emitted file only after the validator reports `PS2DEV_BUNDLE_READY=1`.

## CP47 deterministic target validation orchestrator
Run only after CP45 intake validates a material PS2DEV bundle:
`python3 scripts/run_buildfix27_target_validation.py --bundle-root <bundle> --evidence-dir <dir> --require-complete --pcsx2-cmd '<explicit command using {kernel}>'`
The orchestrator rebuilds wLaunchELF, Project NES and the kernel with the real PS2DEV EE compiler, runs package/R5900 checks, validates PFS target prerequisites, records SHA-256/size for target artifacts and captures the explicit PCSX2 command result. Missing bundle or PCSX2 input remains BLOCKED/NOT_EXECUTED; no host compiler fallback is permitted.

## CP48 physical PS2 evidence gate
After a CP47 target build and actual hardware run, export Titan's Debug Report and record the required event ledger described in `BUILDFIX27_HARDWARE_EVIDENCE_FORMAT.md`. Run `scripts/validate_buildfix27_hardware_evidence.py --require-pass`. Physical validation is promoted only when Reference 720p/output/watermarks/boot history, ten lifecycle cycles, Network, Storage, Titan-owned PFS RW, FTP transactional roundtrip and VMC transactional export all pass.

## CP49 deterministic rehydration
Before target work, reconstruct CP48 from the locked CP14 full source + CP15..CP48 delta chain using `scripts/rehydrate_buildfix27.py`. Do not substitute a mutable workspace or skip SHA verification. `BUILDFIX27_LINEAGE_LOCK_CP49.md` records the exact material chain.
