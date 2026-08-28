# BuildFix27 Checkpoint CP27
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP26
PARENT_ARCHIVE: BUILDFIX27_CP26_INCREMENTAL.tar.gz
BLOCK: Titan Application Runtime / SDK documentation closure

- Added canonical `sdk/titan-app/include/titan_app_sdk.h`, byte-identical to the kernel public ABI.
- Added SDK README, ABI reference, porting guide and minimal application example.
- Documented append-only field gating by the final field actually consumed, rather than unsafe `sizeof(TitanAppApiV1)` gating.
- Documented Storage/VFS atomic publication, async network GET, Titan-owned image presentation, FTP lifecycle/status and ownership prohibitions.
- Documented explicit PFS/VMC and target-toolchain limitations without inventing support.
- Primary `docs/TITAN_APP_SDK.md` updated for BuildFix27 tail and TARGET_BLOCKED truthfulness.
- SDK docs contract, minimal example compile, ABI offset fixture and FTP integration contract pass.

NEXT: CP28 lifecycle leak/cleanup + SAS/runtime classification independence closure.
