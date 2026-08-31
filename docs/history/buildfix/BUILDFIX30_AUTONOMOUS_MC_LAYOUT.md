# BuildFix30 — Autonomous Memory Card layout

Ownership is deliberately separated.

- `mc?:/BOOT/BOOT.ELF` — Titan bootloader, owned by the Titan installer transaction; pre-existing content is preserved by the BF30 backup policy.
- FMCB regional system directory/directories — created by the real FMCB `PerformInstallation()` path according to console/region rules.
- `mc?:/SYS-CONF/FREEMCB.CNF` — FMCB configuration whose automatic launch entries point to the Titan bootloader.
- Other `SYS-CONF` files required by the imported FMCB 1.966 resource set — FMCB-owned bootstrap/configuration material.
- `mc?:/SYS-CONF/TITAN_FMCB.STATE` — Titan-managed FMCB marker/version/state used to distinguish Titan-managed installs.
- Titan transaction staging/backups — `.TITAN.NEW` / `.TITAN.BAK` naming and existing BF30 transaction bookkeeping where applicable.

FMCB does not own or delete Titan `BOOT/BOOT.ELF` during Titan-specific cleanup. Saves and unrelated Memory Card directories are outside the installation ownership set.
