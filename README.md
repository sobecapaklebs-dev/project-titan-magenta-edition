# Project Titan Magenta Edition

<!-- TITAN_SYSTEM_IDENTITY_BEGIN -->

<div align="center">

# ⚠️ **PROJECT TITAN IS NOT A LAUNCHER** ⚠️

## **PROJECT TITAN IS AN OPERATING SYSTEM / SYSTEM PLATFORM FOR THE PLAYSTATION 2.**

### **IT CAN LAUNCH GAMES AND APPLICATIONS, BUT LAUNCHING CONTENT IS ONLY ONE OF ITS RESPONSIBILITIES.**

### **IT IS NOT AN OPL FRONTEND, NOT A PS2 GAME-LAUNCHER SKIN, AND NOT JUST A USER INTERFACE.**

</div>

---

## **WHAT PROJECT TITAN ACTUALLY IS**

**PROJECT TITAN REPLACES THE ROLE OF A SIMPLE LAUNCHER WITH A COMPLETE SYSTEM ENVIRONMENT BUILT AROUND ITS OWN BOOT CHAIN, KERNEL SERVICES, SCHEDULING, RUNTIMES, PACKAGE MANAGEMENT AND NATIVE APPLICATION PLATFORM.**

Project Titan includes, among other subsystems:

- **its own boot chain and system kernel**, responsible for bringing up and coordinating the Titan environment;
- **scheduler and runtime services** used to coordinate native applications, background work and system tasks;
- **native Titan application runtimes and APIs**, allowing applications to run as part of the system instead of behaving only as externally launched ELFs;
- **TPKG/package installation infrastructure**, including a package installer and an installable application format;
- **system-level storage, input, audio, network, graphics, profile, notification, library and VMC services** shared by applications;
- **native application optimization and resource-management systems**, including Titan-specific scheduling, caching, rendering and memory/VRAM strategies designed around PlayStation 2 constraints;
- **integrated game execution backends**, including Titan Neutrino and Titan UL, which provide game-launching functionality as one subsystem of the larger operating environment;
- **a native application/game platform and SDK**, so software can target Titan itself rather than treating Titan as nothing more than a menu that launches another executable.

# **A GAME LAUNCHER STARTS CONTENT. PROJECT TITAN ALSO MANAGES THE ENVIRONMENT THAT CONTENT RUNS INSIDE.**

If you only look at the HOME screen, Project Titan can resemble a console launcher because launching games and applications is one of the jobs exposed by its shell. **That shell is only the visible front end of a much larger system architecture underneath it.**

Project Titan Magenta Edition is therefore best understood as a **PlayStation 2 operating-system project and native application platform that also includes game-launching capabilities**, not as a replacement skin for OPL or a standalone PS2 launcher.

<!-- TITAN_SYSTEM_IDENTITY_END -->

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
