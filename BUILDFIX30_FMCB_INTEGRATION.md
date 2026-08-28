# BuildFix30 — FMCB autonomous integration

BF30 autonomous installation has one release backend: **Free McBoot**. OpenTuna, Fortuna and FunTuna are not connected to the production autonomous path.

The integration reuses the supplied FMCB install algorithm rather than replacing it with file-copy stubs. The Titan-patched FMCB installer retains the upstream `PerformInstallation()` path and `libsecr`/KELF signing behavior, adds a non-interactive `--titan-auto` entry, independently validates Boot ROM version, installs FMCB, writes Titan-managed state, finalizes the two-phase commit, and transfers control to `mc?:/BOOT/BOOT.ELF`.

Product support is fail-closed: a known Boot ROM version must be `<= 2.20`, console identity must remain internally coherent, FMCB media must be complete, the selected Memory Card must have sufficient free space, and an unmanaged/unknown/corrupt existing FMCB installation is not overwritten automatically.

Semi-autonomous behavior remains separate and unchanged: external Matrix/Modbo DEV1/R1 launches the Titan bootloader from the Memory Card without requiring FMCB.
