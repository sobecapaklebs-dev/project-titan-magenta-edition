# BuildFix30 CP18 — Complete Global Regression / Pre-Hardware Fine Closure

Baseline: BF30 FINAL R2 + CP15 FMCB Source Closure + CP16 FMCB Provenance Audit + CP17 FMCB Pre-Hardware Fine Audit.

## Result

- GLOBAL_HOST_REGRESSION_PASS=1
- PROJECT_TITAN_SOURCE_VALIDATION=PASS
- FMCB_SOURCE_AUDIT=PASS
- FMCB_CONTRACTS=PASS
- CURATED_ASSET_VERIFICATION=PASS
- TARGET_BUILD_VALIDATED=0
- HARDWARE_VALIDATED=0

The official host runner was completed in exact script-boundary continuation segments because a single invocation exceeds the executor command wall-clock limit. Timeouts were never counted as PASS; each continuation resumed at an already observed completed boundary, with overlap where necessary. The final segment exited 0 after the FMCB/autonomous release contracts. The remaining outer validate_source stages also passed.

## Regression defect found and corrected

The curated UI asset integrity manifest was stale relative to the long-standing approved material asset set. Four approved files have historically existed under these names:

- avatars/avatar_034.png
- avatars/avatar_134.png
- titan_ui_icons_preview_0340.png
- titan_ui_icons_preview_0341.png

Their bytes match hashes already present in the curated manifest, but the manifest pointed those hashes at nonexistent 033/133/0330/0331 paths and additionally contained two orphan avatar hashes for files absent from the historical source archives. This made the official outer source validation fail even though the protected material assets themselves were unchanged.

Correction: preserve all approved asset bytes and filenames; repair only CURATED_ASSET_SHA256SUMS to describe the actual protected set. No image asset was edited or regenerated.

Corrected curated manifest SHA-256: 743b0c0a441d09e217135f3b2ed77c8a5ea45a19172490bb037cbf89c17bb10d

Verification after correction: curated-assets: OK files=238.

## Full host coverage

PASS coverage includes historical storage/VMC, Neutrino/UL, Package Installer, TPKG, native apps, Project NES/FCEUmm, network/browser/FTP, TGR/pikaVR/VlombisCore, renderer/480i/720p, themes v1-v4, lifecycle/recovery, Unified Input, DualSense/Xbox/Bluetooth/Early Input, System Button/controller UX, Store/Download Service, date/time/NTP, Theme lifecycle, Disc Service/Engine, localization, boot/system packaging, PUP, API freeze, FMCB autonomous install, ROM <=2.20 gating, transaction/rollback/recovery, no OpenTuna/Fortuna path, and semi-autonomous non-regression.

## External gate

PS2 target compilation and physical FMCB/MagicGate/KELF cold-boot validation remain external. No target or hardware PASS is inferred from host validation.
