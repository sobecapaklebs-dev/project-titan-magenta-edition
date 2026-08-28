# BuildFix30 CP19 — FMCB fully integrated Titan installer

Baseline: BF30 FINAL R2 + CP15 FMCB Source Closure + CP16 Provenance Audit + CP17 Pre-Hardware Audit + CP18 Global Regression Closure.

## Product correction

- Autonomous FMCB installation now executes inside `titan-installer.elf`; there is no user-visible or packaged secondary FMCB installer ELF.
- The kernel links `libtitanfmcb_integrated.a`, which retains the audited FMCB 1.966 `PerformInstallation()`, `CalculateRequiredSpace()`, `SignKELF`/`TwinSignKELF`, SECRSIF/MCTOOLS and regional logic.
- FMCB UI/pad surfaces are replaced by callbacks into the Titan installer progress/status UI; Titan remains the visible environment for the whole operation.
- The production media builder packages only FMCB resources plus the Titan-owned FMCB boot entry; it rejects the historical generic EXPLOIT path.
- Method UX is fail-closed: known compatible consoles get **Semi-autonomous + Autonomous**; incompatible/unknown consoles get **Semi-autonomous only** and cannot select autonomous by input.
- Compatibility remains `ROM <= 2.20` plus known/consistent model and region identity.
- Integrated-mode transaction journal recovery survives interruption between Titan publication and FMCB completion.
- Semi-autonomous DEV1/Matrix/Modbo path remains `mc?:/BOOT/BOOT.ELF`.

## Validation in this executor

- focused FMCB CP19 contracts: PASS
- BuildFix30 acceptance-gap contract: PASS
- boot/system packaging contracts: PASS
- BuildFix30 release-regression meta contract: PASS
- installer POC legacy contract: PASS
- ROM compatibility ASan/UBSan contract: PASS
- `titan_source_audit.py`: PASS
- Python syntax for changed tools/contracts: PASS
- production scan for `FMCBInstaller.elf`, `--fmcb-resume`, `FMCB_HANDOFF`, `--titan-auto`: NONE outside vendored upstream/provenance

## External gate

This executor has no PS2DEV/PS2SDK or `mips64r5900el-ps2-elf-gcc`; therefore target link and physical MagicGate/KELF/cold-boot validation are not claimed.

`SOURCE_HOST_VALIDATED=1`
`TARGET_BUILD_VALIDATED=0`
`HARDWARE_VALIDATED=0`
