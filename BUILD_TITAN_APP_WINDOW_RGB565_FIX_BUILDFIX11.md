# Project Titan Magenta Edition 0.3.90-rc14 — Titan App Window / RGB565 BuildFix11

## Hardware symptom addressed

Project Nes installs and launches as a Titan-native app and can return to Home, but the first physical-PS2 media presentation is visually corrupted: translucent/ghosted NES pixels and streaks appear over the Magenta shell.

## Root cause

Titan App API v1 correctly documents application video as conventional RGB565. The GS backend, however, uploaded those 16-bit values directly to `GS_PSM_16`. The PlayStation 2 PSMCT16 layout is R5G5B5A1, so raw RGB565 bits were interpreted with the wrong channel placement and the high color bit could become texture alpha. This explains both the wrong colors and shell/background leaking through the emulated frame.

Project Nes itself already generated a complete 512x256 RGB565 media surface with stride 512. Its framebuffer production does not need an app-local workaround.

## Fix

- Preserve the public Titan App media contract as conventional RGB565.
- Convert RGB565 -> GS PSMCT16 with opaque A=1 before upload.
- Apply the conversion in both renderer paths:
  - legacy/480i cached-layer upload;
  - native 720p 256x64 streamed media tiles.
- Mirror the same conversion in the host/software renderer so validation matches hardware semantics.
- Keep all existing GS/VRAM allocations unchanged. Native 720p still reports zero spare VRAM and reuses the existing avatar scratch texture.

## Titan App composition

Titan-native apps are now windowed by default:

- Home background/header/grid remain frozen and visible behind the app as a live-area frame.
- The app keeps drawing in the public 854x448 coordinate space; Titan applies a centered 0.72x viewport transform.
- The app window has Titan-owned dark chrome/shadow and a Magenta top rule.
- R3 is reserved by the shell to toggle windowed/fullscreen mode for every Titan App.
- Select+Circle remains the kernel-owned emergency exit.
- Global notifications and DEX overlay remain above the app.
- No Titan App ABI version bump is required.

## Project Nes audit

- `media_frame` is a full 512x256 `uint16_t` surface.
- `media_present_rgb565(..., 512, 256, 512)` matches the Titan API contract.
- The NES 256x240 frame is cleared/scaled into that full surface before presentation.
- The app uses the Titan media API and does not touch GS state directly.
- No emulator-specific color/stride workaround was added; the system renderer owns the format conversion.

## Validation

`bash scripts/validate_source.sh` passes, including:

- Titan App ET_REL/R5900 runtime validation;
- Package Installer / TPKG tests;
- renderer 480i/720p VRAM contract;
- new `titan-app-window-media-contract` with RGB565 color oracles;
- Project Nes CPU/PPU and PCM host tests;
- storage, audio, DEX, Home, UTF-8, hardening and release contracts.

Curated UI assets remain byte-for-byte identical to BuildFix10 (241 files).

## Physical PS2 boundary

This environment cannot compile the final EE binary with the user's PS2DEV toolchain or execute the corrected GS path on physical hardware. The hardware test remains authoritative.
