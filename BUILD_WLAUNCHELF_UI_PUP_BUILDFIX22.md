# Project Titan Magenta Edition RC14 — BuildFix22

Base: `Project-Titan-Magenta-Edition-0.3.90-rc14-buildfix21-wlaunchelf-ui-pup-source.tar.gz`

This build fixes the two issues reproduced on hardware after the first BuildFix21 test:

1. **wLaunchELF tile launch failure** showing `titan_app_init rejeitou a sessao`.
2. **System update failure** showing `Checksum da atualizacao invalido` during TPUP extraction.

## Fixes included

### 1) wLaunchELF / Titan App compatibility handling

- `apps/wlaunchelf/src/wlaunchelf_titan.c`
  - adds a dedicated init return code for **incompatible runtime**.
  - validates only the ABI tail actually required by wLaunchELF (up to `launch_elf`).
  - cleanly distinguishes a bad session from an **older kernel/runtime** that does not yet expose the storage/launch extension.

- `kernel/src/services/titan_app_runtime.c`
  - maps that dedicated init code to a clearer user-facing error:
    - `Titan App requer um runtime mais novo (atualize o sistema)`

This means that if the app is launched on an older Titan build, the failure is now explicit instead of looking like a generic Titan App rejection.

### 2) TPUP updater checksum hardening

- `kernel/src/services/titan_system_update.c`
  - now **re-seeks the PUP input stream before every chunk read**, avoiding read-position drift during multi-file extraction on USB.
  - now **re-verifies each extracted staged file** (size + CRC32) before publish.
  - reports a storage positioning error if the package stream cannot be re-positioned correctly.

This directly targets the hardware symptom where the first `.pup` test reached extraction and then failed with `Checksum da atualizacao invalido`.

## Build / packaging changes

- `TITAN_SYSTEM_UPDATE_CURRENT_BUILD` advanced to **22**.
- `make -C kernel pup` now generates:

```text
Project-Titan-Magenta-Edition-0.3.90-buildfix22-updater-wlaunchelf-fixes.pup
```

- The PUP payload remains:

```text
kernel.elf
TITAN/packages/WLAUNCHELF.pkg
```

## Validation performed in this environment

- TPUP build/verify tool contract: PASS
- BuildFix22 updater + wLaunchELF contract: PASS

No PS2DEV MIPS toolchain is available in this environment, so the final `kernel.elf`, `.tmod`, `.pkg` and `.pup` binaries still need to be produced on the user's PS2DEV host.
