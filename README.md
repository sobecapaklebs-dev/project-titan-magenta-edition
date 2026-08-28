# Project Titan: Magenta Edition

> An experimental PlayStation 2 system shell, runtime and application platform focused on modern UX, native applications, storage/runtime services, themes, networking and extensibility.

**Initial public release posture:** release candidate, **Semi-autonomous / DEV1 installation supported**. The Free McBoot autonomous installer remains in source as experimental work and is **disabled from the initial public release media**.

Project Titan: Magenta Edition is a community-oriented PS2 software project built around an Orbis-inspired shell and a native runtime/SDK. The project explores how far the PlayStation 2 can be pushed as a modern homebrew platform while keeping real-hardware behavior as the final authority.

## Highlights

- Orbis-inspired shell with profiles, settings, notifications, folders and system-style transitions.
- Native Titan application runtime and package format.
- Titan SDK / Game SDK for community applications and ports.
- ISO / ELF / UL integration through Titan-Neutrino and Titan UL.
- Per-profile virtual memory-card workflow.
- Theme engine with static and animated scenes.
- `pikaVR` VRAM residency / texture-management work.
- `VlombisCore` scheduling, storage, audio, network and caching work.
- Package Installer and update infrastructure.
- USB storage, Ethernet and experimental Wi-Fi support.
- Accessories / controller work including modern controller support.
- Experimental Free McBoot autonomous-installer source retained for future validation.

## Installation support in the initial release

The supported installation method for the initial public release is:

```text
Semi-autonomous / Matrix-Modbo DEV1
    -> mcX:/BOOT/BOOT.ELF
    -> Titan Bootloader
    -> /kernel.elf on USB
    -> Project Titan
```

`BOOT.ELF` is intentionally uppercase. The initial release media does not expose Autonomous installation.

The Free McBoot integration is **not being removed from the source tree**. It remains experimental and is planned for a later release after broader physical-console, MagicGate/KELF, rollback and cold-boot validation.

See [Release Status](docs/RELEASE_STATUS.md) and [Installation](docs/INSTALLATION.md).

## Hardware status

The current candidate has been exercised on physical PlayStation 2 hardware for the core semi-autonomous path, including installer completion, DEV1 cold boot, library application scanning, Package Installer transactions, Titan Native applications and the current Welcome-screen presentation.

This does **not** imply that every supported controller, network adapter, game, display mode or console revision has been exhaustively validated. See [Hardware Testing](docs/HARDWARE_TESTING.md) and [Known Issues](docs/KNOWN_ISSUES.md).

## Important legal / redistribution note

This repository is intended for Project Titan source code, documentation and redistributable assets only.

Do **not** commit or distribute:

- commercial game ROMs, ISOs, BIOS dumps or decrypted game data;
- Sony proprietary system software;
- copyrighted third-party artwork without permission;
- firmware blobs unless their redistribution license explicitly allows it;
- private test packages that embed commercial game data;
- credentials, tokens, personal logs or device-specific dumps.

Project Titan does not require commercial game data to be stored in the public repository. Users are responsible for providing any legally obtained game content required by a private port or test setup.

See [Licensing and Third-Party Components](docs/LICENSING.md).

### Magenta Melody asset

The public source export intentionally does **not** redistribute
`runtime/TITAN/audio/magenta_melody.wav`. The tested private candidate used a
user-provided music source, so that audio cannot be assumed redistributable.
The runtime treats the menu music as optional; developers may provide their own
compatible PCM WAV asset for private builds.

<!-- RC1_COMMUNITY_TESTING -->
## Community RC1 testing

The prerelease is not limited to checking whether the installer finishes.
After a successful DEV1 boot, reports are also useful for USB storage, Ethernet,
**experimental Wi-Fi**, controllers/accessories, VMC, Package Installer, Titan
Native applications, Titan-Neutrino / Titan UL, themes, audio continuity and UI
responsiveness.

- [Download Installer Test RC1](https://github.com/sobecapaklebs-dev/project-titan-magenta-edition/releases/tag/installer-test-rc1)
- [Community testing guide](docs/PRERELEASE_TESTING.md)
- [Networking](docs/NETWORKING.md)

## Documentation

- [Release status](docs/RELEASE_STATUS.md)
- [Build from source](docs/BUILDING.md)
- [Install / deploy to USB](docs/INSTALLATION.md)
- [Architecture overview](docs/ARCHITECTURE.md)
- [Hardware validation](docs/HARDWARE_TESTING.md)
- [Known issues](docs/KNOWN_ISSUES.md)
- [Release checklist](docs/RELEASE_CHECKLIST.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)
- [Licensing](docs/LICENSING.md)

## Repository layout

The exact layout evolves, but the main areas are:

```text
kernel/          Project Titan kernel, shell and services
bootloader/      Titan Memory Card bootloader
apps/            Native Titan applications
ports/           Application/game ports and integration work
modules/         Runtime/engine modules
scripts/         Build, validation and release helpers
docs/            Project documentation
third_party/     Vendored third-party source, with upstream notices/licenses
```

## Build environment

The target build requires a PS2 development toolchain. The project has primarily been tested with a local PS2DEV / PS2SDK / gsKit setup on Linux.

Typical environment variables:

```bash
export PS2DEV="$HOME/Projects/ps2dev"
export PS2SDK="$PS2DEV/ps2sdk"
export GSKIT="$PS2DEV/gsKit"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PATH"
```

See [BUILDING.md](docs/BUILDING.md) for the full workflow.

## Project philosophy

Project Titan aims to preserve the strengths of the PlayStation 2 while providing a cohesive modern shell, a documented native application model and reusable platform services. Hardware correctness is preferred over emulator-only behavior, and release claims should be backed by real-device validation.

## License

Original Project Titan code is licensed under the [MIT License](LICENSE), except where individual files or components state otherwise. Third-party components retain their upstream licenses; notably, FCEUmm is GPL-2.0 and Project NES distributions linked with it follow the documented GPL combined-work boundary. See [Licensing and Third-Party Components](docs/LICENSING.md).

---

Project Titan is an independent homebrew project and is not affiliated with or endorsed by Sony Interactive Entertainment.
