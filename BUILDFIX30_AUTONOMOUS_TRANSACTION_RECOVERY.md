# BuildFix30 CP19 — Autonomous transaction and recovery

The autonomous installation remains one Titan transaction even though it reuses the audited Free McBoot installation/signing engine internally.

1. **Preflight** — validate TINS, known console identity, `ROM <= 2.20`, FMCB resource bundle, Memory Card state and free space. Unknown/incompatible hardware never enables autonomous mode.
2. **Titan stage/publication** — enabled TINS entries are staged as `.TITAN.NEW`, CRC-verified, existing destinations are backed up and verified, then published.
3. **Integrated-backend journal** — immediately before the internal FMCB engine starts, Titan writes `TITAN_FMCB_TXN=1`, the selected Memory Card and the per-entry transaction state.
4. **FMCB integrated phase** — the same `titan-installer.elf` calls `TitanFmcbBackend_Install()`, which invokes upstream `PerformInstallation(..., INSTALL_MODE_FLAG_TITAN_MINIMAL)` and the retained MagicGate/KELF/space logic. No `LoadExecPS2` or standalone FMCB installer is part of this phase. FMCB UI hooks are mapped to Titan progress callbacks.
5. **Titan commit** — only after the integrated backend succeeds does Titan write `INSTALL.COMPLETE`, clean transaction backups/journal, consume the one-shot installer, and offer reboot/boot.

If the integrated FMCB phase fails in the current run, the existing Titan rollback restores Titan-owned destinations. If power is lost after the integrated-backend journal is written, the next Titan installer launch validates the journal against the TINS manifest, cleans the clean-card FMCB partial state through the integrated backend, restores Titan IOP/storage services, and performs the normal Titan rollback. A malformed or inconsistent journal fails closed rather than starting another install.

Semi-autonomous installation never enters the FMCB backend and continues to publish the validated Titan bootloader at `mc?:/BOOT/BOOT.ELF`.
