# Architecture Overview

## 1. Platform layers

Project Titan is organized as a set of platform layers rather than a single monolithic launcher.

### Boot / installer layer

The boot chain is responsible for entering the Titan loader/kernel from supported PS2 boot environments. The initial public release supports the Semi-autonomous Matrix/Modbo DEV1 path through uppercase `mcX:/BOOT/BOOT.ELF`. Integrated Free McBoot autonomous-install source is retained as experimental work but is disabled from the initial release media.

### Kernel and shell

The kernel owns:

- shell lifecycle and UI pages;
- input routing;
- storage and library services;
- notifications and system state;
- network / accessory service integration;
- application launch boundaries;
- theme/background rendering;
- diagnostics and debug reporting.

### Titan Runtime

The Titan Runtime provides a native application model and lifecycle separate from conventional ELF launching. Native applications are packaged with metadata, entrypoint/module data and optional UI assets.

### Storage and library

The library scanner discovers supported game/application content and publishes a catalog to the shell. Storage operations should remain bounded and should not block the frame path.

Package installation is intended to be transactional:

```text
validate -> stage -> verify -> commit -> publish
```

Incomplete `.install-*` staging directories must never be treated as installed applications.

### Rendering

The shell renderer supports multiple display modes while keeping 480-line backgrounds as the primary application-background contract.

Application artwork contract:

```text
background.rgb565  640x480 RGB565 primary
                   512x256 RGB565 legacy fallback

tile.rgb565        256x256 RGB565

icon.rgb565         64x64 RGB565
```

When an application background is active, it replaces the normal wave/ribbon presentation for that surface rather than being composited as an unrelated extra layer.

### pikaVR

`pikaVR` is the Project Titan VRAM/resource-management work. Its scope includes residency, CLUT/format pressure, upload avoidance, cache behavior and telemetry.

### VlombisCore

`VlombisCore` is the umbrella name used for scheduling/platform work around storage, audio, network, caching and frame-budget behavior.

### Game / application providers

Project Titan integrates multiple content paths:

- Titan Native applications;
- Titan Native Game applications;
- ELF applications;
- ISO / Neutrino content;
- UL / USBExtreme content.

The shell should keep animation, metadata badges and launch presentation consistent across those providers.

## 2. Design rules

1. Real PS2 hardware is the final authority.
2. Blocking filesystem/network work should not run in the frame-critical path.
3. UI transitions should be shared rather than reimplemented per page.
4. Background assets use 480-line primary content, with lower resolutions only as fallback.
5. Native app packages must be transactionally installed before they are visible to the library.
6. System text must go through the localization layer.
7. Debug telemetry should be strong enough to identify the exact lifecycle/storage phase of a hardware failure.

## 3. Public/private separation

Private ports that depend on commercial game content should remain separate from the public source distribution. Public code should never contain commercial ROM data or packages that embed it.
