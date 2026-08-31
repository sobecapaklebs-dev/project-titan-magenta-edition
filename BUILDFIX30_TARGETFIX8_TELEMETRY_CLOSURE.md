# Project Titan — BuildFix30 TARGETFIX8 Telemetry Closure

## Input evidence

Authoritative compile telemetry: `20260821T222314Z`.

Observed sweep state:

- PASS: 7
- FAIL: 5
- SKIP: 4
- extracted error lines: 89
- unique error lines: 78
- undefined linker symbols: 0
- failing stages: `kernel-target`, `project-nes-target`, `neutrino-target`, `titan-ul-target`, `fmcb-integrated`

This closure starts from the exact source authority `R4-AUDITED + TARGETFIX1..TARGETFIX7`. The uploaded R4 archive alone is not treated as the latest working tree.

## Concrete corrections

### 1. kernel-target

- Preserved TARGETFIX4's current-PS2SDK `_gp` declaration correction (`extern void *_gp`) in DateTime and Early Input.
- Corrected the TARGETFIX6 HID regression: `fileXioOpen()` now uses the current `O_RDONLY` flag and explicitly includes `io_common.h`; the non-existent `FIO_O_RDONLY` token is gone.

### 2. project-nes-target

- Kept the frozen FCEUmm provider source byte-identical.
- First-party Project NES code remains under `-Werror`.
- Added only `-Wno-error=parentheses` to `FCEUMM_CFLAGS`, next to the existing vendor-only unused-parameter exception. This closes GCC 15 precedence diagnostics in `126_422_534.c` without rewriting the frozen third-party mapper implementation or weakening first-party warning policy.

### 3. neutrino-target

The real linker failure was `.bss` ending at `0x94810` while `ee_core84` ends at `0x94000`.

- The Titan Smart IGR worker has a dedicated 4 KiB stack (`IGR_Stack`). It is now emitted into a dedicated NOLOAD section `.titan_igr_stack` at `0x95000`, instead of consuming `ee_core84` `.bss`.
- The normal EE core stack remains unchanged at `0x94000..0x95000`.
- Default module storage moves from `0x95000` to `0x96000`, so it cannot overlap the resident IGR stack.
- `modules84` is correspondingly `0x96000..0x100000` (424 KiB).
- Loader configuration and the 128 KiB checksum-range documentation were updated consistently.

Removing the 4 KiB IGR stack from the failing `.bss` leaves the previously observed core footprint below the `0x94000` boundary rather than extending the core into its primary stack.

### 4. titan-ul-target

- Fixed the EE core include search order. `-Iinclude` now precedes `-I../include`.
- This makes `#include "util.h"` resolve to `ee_core/include/util.h`, which owns `_strncmp`, `_memcmp`, `_strcpy`, `_strcmp`, `CopyToIop`, `WipeUserMemory`, `set_ipconfig` and `find_pattern_with_mask`.
- No fake prototypes or duplicate implementations were introduced.

### 5. fmcb-integrated

- Extended the integration-only compatibility header with `iox_stat.h`, restoring current PS2SDK `FIO_S_*` stat/mode definitions used by the frozen upstream installer.
- `system.c`, `ReqSpaceCalc.c` and `libsecr.c` are now all compiled through the same forced integration compatibility header.
- The existing integration-only `sio_printf -> printf` diagnostic bridge therefore reaches both upstream call sites that failed in the telemetry.
- Upstream FMCB/MagicGate/KELF implementation files remain unchanged.

## Validation performed in this executor

PASS:

- BuildFix30 target-source audit contract, extended with TARGETFIX8 invariants.
- Complete BuildFix30 FMCB release-blocker contract suite, including install layout, ROM gate, backup/stage/write/verify/commit, rollback, interrupted recovery, Titan boot chain, recovery and semi-autonomous non-regression.
- BuildFix28 keyboard/mouse contract.
- Neutrino overlay, resume and return contracts.
- Titan UL overlay, resume and return contracts.
- Project NES provider and FCEUmm adapter contracts.
- Project NES complete host-test suite, including ASan/UBSan adapter test and provider packaging gate.
- Python AST parse: 443 files.
- Shell `bash -n`: 175 files.
- `kernel/tools/titan_source_audit.py`: PASS; 14 allowlisted binary payloads, 31 license/notice files.
- Frozen FCEUmm tree byte comparison against the TF7 authority: unchanged.
- Host GNU ld structural validation of the Neutrino linker change: `.titan_igr_stack` is a 4 KiB NOBITS/NOLOAD section at `0x95000`, outside the normal `.bss` and primary EE stack.

The broader `scripts/validate_source.sh` run was also started and passed a long sequence of host contracts through `source-hardening`, but the executor terminated that monolithic run on its execution-time limit. It is therefore not reported as a complete PASS.

## Remaining validation gate

`TARGET_BUILD_VALIDATED=0` in this executor because `mips64r5900el-ps2-elf-gcc`, `PS2DEV` and `PS2SDK` are not installed here. No claim is made that a target build was run locally.

The next real gate is to run the same diagnostic sweep in the pinned PS2DEV environment. If these five target stages pass, the four downstream stages that were skipped by the supplied telemetry can execute: `neutrino-copy`, `titan-installer`, `runtime-assemble`, and `installer-media`.

Recommended command from the corrected tree:

```bash
bash scripts/build_all.sh --diagnostic-sweep
```

A physical PS2 test remains required after a clean target build, especially FMCB/MagicGate cold boot, installer rollback/recovery, input transports and Neutrino/Titan UL game return paths.
