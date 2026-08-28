# BuildFix30 FMCB — Pre-hardware fine audit

Status: HOST/SOURCE AUDIT PASS after correction. Target build and physical PS2 validation remain pending.

## Rehydrated baseline
- BuildFix30 FINAL SOURCE R2
- CP15 FMCB Source Closure delta
- CP16 FMCB Provenance Audit delta

## Defect corrected
The CP16 source-release audit referenced `third_party/fmcb/upstream/README.md`, but the imported, identity-preserving upstream document is named `README.upstream.md`. It also allowlisted the upstream 1.966 resource locations but not the byte-identical curated copies under `third_party/fmcb/resources/INSTALL`, causing a false release-audit failure.

Correction:
- provenance requirement now points to the actual imported `README.upstream.md`;
- curated FMCB 1.966 ELF/IRX runtime copies are hash-locked individually and require both the BF30 provenance report and FMCB source identity record;
- fail-closed hash behavior remains intact.

## Validation performed
- `python3 kernel/tools/titan_source_audit.py`: PASS, 14 approved payloads, 31 license/notice files.
- all project Python outside vendored third-party/build trees: 429 files compiled with `py_compile`, 0 syntax errors.
- all shell scripts: 174 files checked with `bash -n`, 0 syntax errors.
- FMCB release-blocker contract: PASS for source integration, provenance, build wiring, ROM <=2.20 gating, install layout, config, transaction, rollback, recovery, boot chain, no OpenTuna/Fortuna, and semi-autonomous non-regression.
- broad `scripts/validate_source.sh` was run until the executor time limit; all emitted checks through the observed point were PASS/OK. The run did not finish before timeout, so this is not represented as a complete global-suite pass from this audit.

## External gate
The local executor does not provide the PS2 target headers/toolchain (`tamtypes.h` unavailable to host cc), so the target C build was not claimed. Physical FMCB MagicGate/KELF install and cold boot must be validated on a real supported PS2/Memory Card.

PREHARDWARE_SOURCE_AUDIT=PASS
FMCB_SOURCE_AUDIT=PASS
PYTHON_SYNTAX=PASS
SHELL_SYNTAX=PASS
FMCB_CONTRACTS=PASS
TARGET_BUILD_VALIDATED=0
HARDWARE_VALIDATED=0
