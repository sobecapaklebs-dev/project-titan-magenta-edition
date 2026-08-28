# Project Titan Magenta Edition

Project Titan Magenta Edition is a system platform for PlayStation 2. It is not
just a game launcher: the maintained tree contains a boot chain, shell/runtime,
profiles, VMC management, library services, audio, renderer, DEX diagnostics,
applications and two integrated game engines.

## Product components

- **Titan Bootloader** — validates and starts the canonical root kernel.
- **Titan Shell and Runtime** — interface, services, profiles, assets and tools.
- **Titan Neutrino Engine** — the project-maintained Neutrino-derived ISO engine.
- **Titan UL Engine** — the project-maintained Open PS2 Loader-derived UL engine.
- **Titan App Runtime/API v1** — in-process MIPS application modules installed from TPKG v2, with shared Titan UI, input, notifications, storage and media-output services.

The Titan names identify the modified and integrated variants maintained here.
Original upstream authorship, licences and notices remain preserved in each
module.

## Canonical source tree

The development folder is always named:

```text
Project Titan Magenta Edition/
```

Version numbers belong in `VERSION`, release tags and packages, never in a new
parallel source folder. Generated objects and release binaries belong under
`build/` and `dist/` and are not tracked.

## Validation and build

Host validation does not require the PS2 toolchain:

```sh
scripts/validate_source.sh
```

A complete clean MIPS build requires `PS2DEV` and `PS2SDK`:

```sh
scripts/build_all.sh
scripts/build_all.sh --verify-reproducible
```

Install the resulting runtime only through the recovery-journal installer:

```sh
scripts/install_to_usb.sh --target "/run/media/$USER/PS"
```

The canonical deployment paths are documented in `docs/ARCHITECTURE.md`.
Community application development is documented in `docs/reference/TITAN_STANDALONE_SDK.md` and
the installable package contract in `docs/TITAN_PACKAGE_FORMAT.md`.
Physical PS2 validation remains mandatory before release; host tests cannot
prove GS timing, IOP behavior or per-game compatibility.

## Current release line

`0.3.91-rc15` introduces the TGR v2 texture/ROM frontier for the TitanEYE port
and the four Ribbon v8 themes. The themes retain their prior palettes while a
coherent transported ribbon frame supplies the reference motion, twist,
edge-on collapse and overscan. The embedded MGB64 tree is pinned and contains
no ROM or extracted game assets. The native game remains a source checkpoint,
not an executable TPKG, until the Fast3D-to-GS backend, TGR host replacements,
remaining ROM-pointer users and in-process image loader are complete. See
`ports/goldeneye/PORT_STATUS.md`. MIPS, PCSX2 and physical-PS2 validation have
not been performed in this environment.
