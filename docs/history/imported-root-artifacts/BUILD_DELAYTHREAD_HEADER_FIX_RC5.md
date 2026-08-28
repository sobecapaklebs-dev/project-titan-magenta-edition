# RC5 Titan Neutrino EE loader compile correction

## Failure reproduced from the hardware-build log

The RC4 reproducible build reached
`modules/titan-neutrino-engine/ee/loader/src/fhi_config.c` and stopped under
`-Werror` because `DelayThread()` had no visible declaration.

## Root cause

`fhi_config.c` included `<kernel.h>` and annotated it as the source of the
`DelayThread()` declaration. The active PS2SDK toolchain exposes that EE API
through `<delaythread.h>`, which the other Project Titan EE call sites already
use.

## Correction

The include was changed to `<delaythread.h>`. The retry remains bounded to 50
attempts with a 10,000 microsecond delay between failed opens. No retry timing,
FHI layout, launch behavior, renderer code, VRAM contract, or asset changed.

## Validation boundary

Host validation, extracted-source validation, source contracts, and byte-level
asset comparison are performed for this package. A successful PS2DEV/PS2SDK
MIPS build and physical PS2 test remain required before release approval.
