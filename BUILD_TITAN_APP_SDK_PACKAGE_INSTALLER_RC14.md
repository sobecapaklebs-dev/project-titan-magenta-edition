# RC14 — Titan App SDK / Package Installer extension

This development revision extends the existing Package Installer without changing Project Titan's visual assets.

Implemented:

- TPKG v2 native/ELF package metadata and required 64x64 RGB565 tile icon.
- In-process Titan App `.tmod` loader and public API v1.
- Shared shell UI primitives, notifications, input, app-local file API and media-output foundation.
- Installed-app uninstall from the Home context menu.
- DEX-only installation enforced in UI and service layer.
- DEX performance overlay toggle persisted independently from DEX Mode.
- Package list displays the real `.pkg` filename; Cross opens a dedicated install-progress view.
- App icons reuse unused cells in the resident avatar atlas; no additional GS VRAM allocation.
- Native 720p media frames stream through the existing PSM16 avatar scratch, preserving the proven 4 MiB double-buffer layout.

Public documentation: `docs/TITAN_APP_SDK.md`.

Runtime compatibility notes:

- `.tmod` loader accepts standard non-runtime MIPS ABI metadata sections emitted
  by current GCC/Clang (`.reginfo`, `.MIPS.options`, `.MIPS.abiflags`) while
  still rejecting unknown allocated section types.
- The existing avatar atlas provides 57 dynamic app-icon cells without a new GS
  texture; additional installed apps fall back to the generic application icon.
- Media video frames are held in EE RAM. 480i uses the existing cached layer;
  native 720p streams through the pre-existing 256x64 PSM16 avatar scratch in
  tiles, keeping the native 720p GS allocation at exactly 4 MiB / 0 bytes free.
- The media API is an output foundation (PCM/RGB565), not a bundled codec or
  container decoder.
