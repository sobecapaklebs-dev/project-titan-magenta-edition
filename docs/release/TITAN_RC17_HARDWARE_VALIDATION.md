# Project Titan Magenta Edition — RC17 hardware validation

Date: 2026-08-28

Validated system baseline:

- Kernel: `0.3.93-rc17`
- System updater build: `38`
- Native target ABI: R5900 N32 / `EF_MIPS_ABI2`
- Public developer SDK: `Project-Titan-SDK-2026-08-28-FINAL-R2.zip` (distributed separately)

Physical-hardware validation completed for the native launch chain:

- Titan native application runtime launch: PASS
- Titan Game Runtime (TGR) launch / ownership handoff: PASS
- storage handoff / worker scheduling fix introduced by RC17: PASS
- TGR game-thread stack ownership for ordinary local data: PASS
- TitanEYE native runtime launch into the GoldenEye port: PASS

TitanEYE still has port-specific gameplay/compatibility bugs. They are documented
as known issues and are not treated as failures of the Titan native runtime or
as blockers for this Magenta Edition source release.

This record is deliberately scoped to the paths actually exercised on physical
hardware. It does not claim that every optional device, network backend, console
revision or third-party port behavior was hardware-validated.
