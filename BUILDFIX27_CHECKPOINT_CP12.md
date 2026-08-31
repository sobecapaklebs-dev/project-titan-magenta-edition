# BuildFix27 Checkpoint CP12
STATUS: HOST_VALIDATED
PREVIOUS_VALID: CP11
BLOCK: Project NES ROM-free mapper/region/SRAM/audio conformance + provider readiness

- Added platform-neutral `ProjectNesCoreRomInfo` + ROM probe boundary.
- Classic iNES mapper/region/battery/trainer metadata is normalized without Titan API ownership.
- NES 2.0 format, 12-bit mapper and submapper/timing hint are identified without pretending to normalize complex NES2 ROM-size encoding.
- Legacy NROM provider explicitly rejects unsupported mapper and NES2 execution.
- ROM-free fixture covers mapper 0/1/0x123, PAL metadata, battery SRAM and unsupported-provider results.
- Timing fixture proves provider 60.000 Hz and 533/533/534 PCM cadence = 3200 frames per 0.1 s at 32 kHz.
- CP11 SRAM lifecycle contract and CP10R recovery contract remain green.
- FCEUmm provider remains NOT_MATERIALIZED; no mapper expansion is claimed.

Contracts:
- buildfix27-project-nes-conformance-contract: PASS
- buildfix27-project-nes-storage-save-contract: PASS
- buildfix27-cp10-recovery-contract: PASS
- strict conformance fixture compile/run (`-Wall -Wextra -Werror`): PASS
- Project NES host core/audio/lifecycle/save/conformance suite: PASS

NEXT: CP13 Project NES repeated lifecycle/fault cleanup + provider/package readiness closure.
