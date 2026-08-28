# Project Titan BuildFix24 Validation

## Final state
- BASELINE_PROVENANCE: CONFIRMED
- BASELINE_BUILD: BuildFix23 FINAL
- BASELINE_SHA256: ae312f94e3b0edb5400e0642051732222c2bd001f78f36a46bfb63afec4bdb26
- CP00..CP12: completed in order.
- HOST VALIDATED: YES
- R5900 CROSS-BUILD VALIDATED: NO — NOT EXECUTED (toolchain unavailable)
- PCSX2 VALIDATED: NO — NOT EXECUTED (PCSX2 unavailable)
- PS2 HARDWARE VALIDATED: NO — NOT EXECUTED (physical target required)

## SAS
BuildFix24 separates SAS compatibility from compliance and enforces compliant=>compatible. `title.cfg` is canonical metadata for the audited classic profile; `app.cfg` remains Titan fallback. `INSTALL.PBT` is neither required nor proof. APP is the only frozen normative prefix from the audited evidence; unknown 3-letter prefixes remain compatibility hints. Classic flat/OSDSYS manageability, extended subfolders and copyability are modeled separately. Host validator and TAR/PAX safety gate are included.

Fixture migration: compatible 5 -> 5; compliant 2; compatible/non-compliant 3; no functional fixture disappeared.

## 720p
Release default remains REFERENCE. Optimized 960x540 remains DEX opt-in. DEX selector switches A/B without rebuild and restores the prior path if active-720p reconfiguration fails. Debug export includes path, internal/output resolution, VRAM used/free/reserved, texture residency/cache counters and frame timing. Target screen matrix is included. No promotion occurred because target evidence is absent.

## Regressions
BuildFix23 pikaVR, UI transition/focus/text/collision, Theme Context, dynamic Theme Engine, TitanEYE, TGR, renderer 480i/720p, Ribbon and VU1 contracts pass on host. Theme v2/v3 compatibility passes. Release contract passes.

## Important gate semantics
HOST VALIDATED does not imply R5900, PCSX2 or PS2 hardware validation. Unexecuted target gates are not PASS.
