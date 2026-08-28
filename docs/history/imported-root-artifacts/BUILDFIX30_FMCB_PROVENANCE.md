# BuildFix30 — Free McBoot provenance

## Source of truth

The FMCB integration is based on the user-supplied archive `FreeMcBoot-Installer-master.zip` received for the BF30 release-blocker correction.

- Archive SHA-256: `ed8cfe51704de1e48627b062ecd4b19efbeb475ac0a66e32c30f5fa683fd8999`
- Imported upstream tree: `third_party/fmcb/upstream/`
- FMCB resource set: `installer_res/1966`, identifying Free McBoot 1.966 resources.
- The supplied archive contains no `.git` metadata. Therefore an exact commit identity cannot be proven from the supplied material and is deliberately not invented.
- Upstream README/changelog/notices from the supplied archive are retained verbatim under `third_party/fmcb/upstream/`.

## Attribution and license status

The retained upstream README identifies the custom installer project and credits SP193 as the original FMCB installer author and the historical Free McBoot contributors. Component license/notice files carried by the supplied archive are preserved.

The supplied archive does **not** contain a single top-level license declaration covering every file and binary as one unit. BF30 therefore does not claim a blanket license for the whole imported tree. Redistribution of the imported third-party binary payloads requires final legal/license review before a public source release. This does not affect the source-side integration/validation state.

## Imported material

BF30 imports the installer source required to preserve the real FMCB installation algorithm, including `PerformInstallation()`, `libsecr`, MagicGate/KELF handling, region-aware install logic, memory-card tooling, and the 1.966 install resources. Titan-specific glue is kept separate in `third_party/fmcb/titan/` and in the Titan installer/build tools.

The original user-supplied archive remains the provenance root. Exact third-party executable/IRX payload identities used by the source tree are hash-locked by `kernel/tools/titan_source_audit.py`; a changed hash fails closed.
