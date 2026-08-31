# BuildFix30 — Autonomous Installer (FMCB integrated)

## Product contract

Project Titan exposes **one installer and one UI environment**. Free McBoot is an internal installation backend of `titan-installer.elf`; the release path never launches, chains to, or exposes a second FMCB installer executable.

At System Verification the Titan installer reads `rom0:ROMVER` and the read-only model/region identity. Compatibility is fail-closed. When the console is eligible for FMCB (product ROM gate `ROM <= 2.20` plus known model/region identity), the Method screen offers **Autonomous** and **Semi-autonomous**. If FMCB eligibility is false or unknown, the Method screen contains **Semi-autonomous only**.

## Autonomous transaction

`PRE-FLIGHT -> ROM/MODEL -> FMCB STATE -> MC SPACE -> BACKUP -> STAGE -> VERIFY -> FMCB INTEGRATED -> COMMIT -> CLEANUP`

The integrated backend links the retained FMCB 1.966 installation/signing engine into the Titan installer and calls upstream `PerformInstallation()` with Titan's minimal mode. The retained upstream path still performs `CalculateRequiredSpace()`, MagicGate/KELF signing (`SignKELF`/`TwinSignKELF`) and regional FMCB handling. FMCB progress and errors are translated into Titan status/progress callbacks; FMCB UI/menu/pad code is not the visible installer environment.

Immediately before the FMCB engine runs, Titan writes an autonomous transaction journal. If power is lost during that phase, the next Titan installer launch detects the journal, validates it against the TINS manifest, cleans only the clean-card FMCB partial state allowed by this transaction, restores Titan IOP/storage services, and runs the normal Titan rollback. Invalid recovery metadata fails closed.

## Boot result

`POWER ON -> FMCB -> mc?:/TITAN/BOOT/TITAN_FMCB_ENTRY.ELF -> mc?:/BOOT/BOOT.ELF -> validated Titan BuildFix6 bootloader -> kernel/system`

Semi-autonomous Matrix/Modbo DEV1 remains `mc?:/BOOT/BOOT.ELF` and does not require or install FMCB.
