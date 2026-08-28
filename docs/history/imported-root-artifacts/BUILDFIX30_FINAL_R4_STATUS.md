# BuildFix30 FINAL R4 — FMCB Integrated Pre-Hardware Audited

R4 supersedes R3 as the source candidate for the next target compilation.

The autonomous Free McBoot engine remains integrated inside `titan-installer.elf`; no standalone FMCB installer is part of the production media or UX. Known supported FMCB consoles expose Autonomous + Semi-autonomous, while unsupported/unknown consoles expose Semi-autonomous only.

R4 incorporates the complete source/build/lifetime audit documented in `BUILDFIX30_R4_FULL_SOURCE_AUDIT.md`, including target dependency preflight, PADEMU/DS34 USB+BT hardening, AR9271 startup/flight-recorder lifetime fixes, Asset Service synchronization, native-app network thread cleanup, HTTP streaming bounds, boot-video lifetime fixes, and fail-closed FMCB IOP/rollback hardening.

Host/source regression and final hygiene are closed for this source candidate. Real PS2 target compilation and physical FMCB/MagicGate/KELF/cold-boot validation remain external gates and are not claimed as passed.

- `GLOBAL_HOST_REGRESSION_PASS=1`
- `TARGET_SOURCE_AUDIT_PASS=1`
- `TARGET_BUILD_VALIDATED=0`
- `HARDWARE_VALIDATED=0`
