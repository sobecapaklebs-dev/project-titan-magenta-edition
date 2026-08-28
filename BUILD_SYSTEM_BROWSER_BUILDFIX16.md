# Project Titan Magenta Edition RC14 — System Browser BuildFix16

## Goal

Port the existing Titan Browser POC into the current RC14 tree as an installed
application that remains entirely inside Project Titan. This deliberately does
not use the still-unproven Titan-native `.tmod` protected-context launch path.

## Architecture

`Internet` is TPKG v2 type `system-browser`. Its installed manifest carries the
reserved logical entrypoint `@system-browser`; no executable payload is present.
The actual browser runtime is linked into `kernel.elf` and selecting the tile
routes the shell to `TITAN_SHELL_PAGE_BROWSER`.

Runtime components:

- `titan_browser.c/.h`: URL normalization, history, reduced HTML parser, links,
  browser state and Stream Bridge URL translation.
- `titan_browser_net.c/.h`: bounded PS2IP HTTP worker. Worker creation is lazy
  and begins only when the first network operation is submitted.
- `titan_shell_scene.c`: browser window/page, existing Titan text-input UI and
  controls.

## Stability decisions

- No `ExecPS2`, `LoadExecPS2`, external browser ELF or `.tmod` is used.
- The old external MPEG-2 Stream Player handoff is disabled.
- Download UI is disabled in this first internal build so browser network I/O
  cannot start independent storage writes while the current Titan storage
  ownership model is still being proven on hardware.
- HTTP pages remain bounded by the existing Browser POC page buffer.
- HTTPS remains a Stream Bridge transport design; native TLS was not added.

## Package contract

TPKG v2 now recognizes application type `system-browser`:

- fixed entrypoint: `@system-browser`;
- API version 0;
- no embedded ELF/tmod entrypoint;
- required `icon.rgb565` remains app-local;
- installer publishes/removes the app transactionally like other installed
  applications;
- catalog identifies it as `System App` and Home launch enters the internal
  browser page.

Package:

`Internet-TITN00010-1.1.0-system-browser.pkg`

## Asset preservation

Comparison against BuildFix15:

- `kernel/assets`: 241 files, 0 byte differences.
- `bootloader/assets`: 3 files, 0 byte differences.

The browser icon is package-local under `apps/internet-browser` and does not
replace a curated Titan asset.

## Host validation

`scripts/validate_source.sh` passes, including:

- TPKG builder/verifier including `system-browser` positive and negative cases;
- `system-browser-contract: OK`;
- package installer transaction tests;
- Home/shell contracts;
- R5900 Titan App ISA guard contracts;
- 480i/720p renderer contracts;
- VU1 internal-edge contract;
- source hardening and release contracts.

Changed browser/system-app C sources also pass host `-Wall -Wextra -Werror`
syntax validation.

## Validation boundary

This environment does not contain the user's PS2DEV/R5900 toolchain. A complete
MIPS link of the modified kernel was not performed here. HTTP on physical PS2
also requires the Titan/PS2IP network stack to be initialized on that runtime;
this build does not claim real-hardware network validation.
