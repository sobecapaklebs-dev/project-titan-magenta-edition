# Changelog

All notable changes to MGB64 are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/); this project uses
[Semantic Versioning](https://semver.org/).

Detailed, narrative notes for each released version live in
[RELEASE_NOTES.md](RELEASE_NOTES.md); this file is the concise, scannable index.

## [Unreleased]

## [0.4.0] - 2026-07-12

Promotes the `0.4.0-alpha.1`–`alpha.3` pre-release line (detailed below) to the
first stable **v0.4.0** — the new "Latest" release. On top of the alpha content:

### Fixed
- Continued faithfulness/accuracy hardening validated against a stock-ROM
  reference (weapons, guard AI, patrols, projectiles, visibility, and watch/HUD).
- Ejected shell casings render with the correct tint via a well-defined color
  path.
- The two image-diff regression checks skip cleanly when Pillow is absent, so a
  fresh macOS checkout no longer sees spurious test failures.
- Documentation currency: prebuilt-download availability, macOS build/arch
  notes, and internal-doc boundaries corrected.

## [0.4.0-alpha.3] - 2026-07-10

_Pre-release on the road to v0.4.0._

### Added
- Rumble support: `Input.Rumble` / `Input.RumbleIntensity` route the game's
  Rumble Pak signal to controller vibration.
- Real single-player pause — opening the in-game overlay in solo play freezes the
  simulation (and suppresses the stall watchdog) instead of letting it run
  underneath the menu.
- Keyboard-free ROM selection: automatic scanning plus an in-app ROM browser.
- Bundled `SDL_GameControllerDB`, loaded before pads are opened, so more
  controllers are recognized out of the box.
- Handheld readability: `UI.Scale` scales the launcher and overlay, and
  `UI.LauncherFullscreen` fills the display.
- Settings panel: an advanced (developer/diagnostic) tier, per-tab "Reset to
  defaults", inline help captions, plain-language help text, and correct editing
  of text settings.
- PortMaster / handheld GLES build target (community contribution), kept additive
  to the existing backends.
- Early, opt-in scene-decoration / modern-mesh visual experiments for a single
  level, off by default (a work-in-progress showcase, not a gameplay change).

### Changed
- Authentic full-auto fire cadence is on by default (`Input.FireRateAuthentic`):
  full-auto weapons fire at the N64 tick-scaled rate.
- Fullscreen handling: exclusive fullscreen defaults to native resolution with a
  borderless fallback, Alt+Enter returns to the configured mode, and
  `Video.WindowMode` is applied on the app-shell window path.

### Fixed
- The Automatic Shotgun fires through the shotgun firing path instead of the
  pistol path.
- Thrown (sticky) projectiles clamp their impact point on a wall hit.
- Glass shards use the retail-faithful shard scale.
- The Metal backend honors `Video.MSAA` and alpha-to-coverage.
- The ammo-total HUD restores the retail multiplayer early-out.

## [0.4.0-alpha.2] - 2026-07-09

_Pre-release on the road to v0.4.0 — controller-first and Windows-native._

### Added
- Full gamepad navigation for the launcher and in-game overlay, rebindable
  player-1 gamepad buttons, names for all 21 SDL controller buttons, and rebinds
  persisted to disk.
- In-app update check: `Game.CheckForUpdates` shows the running version and an
  update banner when a newer release is available.
- Windows: an embedded app icon and version info in `ge007.exe`, a GUI-subsystem
  clean launch, a diagnostic log, and an install/setup guide shipped in the
  portable `.zip`.
- Real application-icon branding across macOS, Windows, and Linux.

### Changed
- Windows crash handling now uses structured exception handling for real
  `[CRASH]` diagnostics, retiring the previous signal/`longjmp` path; the sim
  stall watchdog runs on Windows too.
- Establishing/detached cameras: the camera-seed portal walk is decoupled from
  the simulation and on by default, filling blank areas in intro/outro/death-cam
  shots.
- Release tooling gained Developer ID signing/notarization support.

### Fixed
- Windows: the GCC runtimes are statically linked so `ge007.exe` runs on a stock
  Windows install; save-path buffer sizing and 64-bit diagnostic formats are
  corrected for the Windows data model.
- Post-intro movement works at the swirl→first-person handoff (Silo).
- The overlay footer no longer claims "Paused" while the sim keeps running.
- Metal shadow-pass correctness fixes.
- Input hardening: the right-stick look is gated while the overlay is open, the
  overlay-close press can't leak a fresh input edge, and Back/Guide from a
  hand-edited gamepad `.ini` is rejected.

## [0.4.0-alpha.1] - 2026-07-09

_Pre-release on the road to v0.4.0._

### Added
- A simulation stall watchdog — a heartbeat plus a forensic stall dump if the sim
  ever stops advancing.
- HUD hardening: a visible fallback for ammo icons when compiled metadata is bad,
  and a numeric health readout.

### Fixed
- Weapon fire rates: the tank cannon fires one shell per trigger press instead of
  full-auto, and the Cougar Magnum and Grenade Launcher fire-rate floor is
  restored.
- Pickups: a magazine pickup grants that clip's real ammunition (not remote
  mines), and a multi-ammo crate hands out the authored slot quantity.
- The first-person muzzle flash is no longer tinted by forced room fog.
- Level intro: the intro weapon no longer overwrites Bond's head, Bond plays his
  scripted intro animation with root motion, and he stays grounded through the
  intro swirl instead of hovering.
- Input: a radial dead-zone and rescale for the movement stick, and mouse-wheel
  weapon cycling advances one weapon per notch.
- Timing: `FrameCap=display` paces to 60 Hz so the sim can't run too fast, and
  HUD/watch timers can't jump on a frame spike.
- Rendering robustness: the color-combiner and texture pools grow on demand
  instead of wrapping/overflowing, N64 display-list recursion is depth-capped,
  invalid texture IDs and light counts are rejected/clamped, the vertex/light
  bump allocators are bounds-guarded, and the Metal minimap is drawn natively.

## [0.3.2] - 2026-07-06

Repository-quality, maintainability, and licensing-correctness pass, plus a
Windows crash fix and one weapon-fidelity fix (both below). Apart from those two
fixes, every runtime edit is verified against the ROM-free CTest suite and is
byte-identical with features off.

### Added
- `docs/README.md` and `tools/README.md` — navigable indexes for the docs and
  tooling trees.
- Registering `GE007_*` flag accessors (`src/platform/port_env.*`) plus a
  generated, drift-gated flag reference (`docs/ENV_FLAGS.md`,
  `tools/gen_env_reference.py`).
- `scripts/ci/ci_local.sh` — one-command local runner for the ROM-free CI gates.
- `src/platform/fast3d/gfx_uniforms.h` — one shared declaration of the render/
  post-FX uniforms consumed by the GL and Metal backends.
- `src/platform/fast3d/PROVENANCE.md` recording the vendored Fast3D renderer's
  origin and reproducing its upstream license notice.
- CTest guards: `port_validation_smoke_registry`, `env_reference_current`,
  `port_env`.

### Changed
- `docs/` reorganized: a curated public reference at the top level and internal
  design/roadmap notes under `docs/design/` (export-ignored). 32 transient
  session/planning artifacts removed.
- The public/internal doc boundary in `.gitattributes` is now structural
  (`docs/design/**`).
- CONTRIBUTING documents a commit-message convention and the env-flag convention.
- `tools/check_third_party_notices.py` now validates the Fast3D license notice
  (copyright line, license identification, and binary clause) so it cannot
  silently regress.

### Fixed
- **AR33 (M16) and RC-P90 fire full-auto again.** Both weapons are full-auto on
  the N64 original, but the port's first-person weapon state machine had grouped
  them with the semi-auto pistols (one shot per recoil cycle). They are now
  routed to the machine-gun firing group the retail jump tables assign them to.
- **Windows: immediate crash on game load.** MinGW/GCC defaults to
  `-mms-bitfields`, which repacks the game's mixed bit-field structs and desyncs
  them from the N64-matching ROM/model/setup data read through them. The engine
  now builds with `-mno-ms-bitfields` on every platform (the macOS/Linux default,
  byte-identical there), so the Windows layout matches and the data parses
  correctly.
- **Fast3D upstream license was mis-identified as MIT.** It is actually a
  modified BSD-2-Clause license (© 2020 Emill, MaikelChan) whose clause 2
  restricts binary redistribution to binaries containing no assets the
  distributor lacks the right to distribute. The reproduced notice was reconciled
  verbatim against the authoritative upstream `LICENSE.txt`, and the
  identification was corrected in the Fast3D source-file headers, `THIRD_PARTY.md`,
  and `PROVENANCE.md`. MGB64's prebuilt binaries are asset-free (verified by
  `check_no_rom_data.sh` / `verify_asset_free.sh`) and so satisfy the binary
  clause.
- Integer-overflow in the ROM DMA bounds check (`src/ramrom.c`) that a crafted
  ROM image could use to read past the loaded buffer.
- Removed 38 phantom validation-smoke registrations that pointed at scripts which
  were never committed (advertised coverage now matches what ships).
- Removed a dead, build-excluded copy of `gfx_pc.c` with a misleading header.
- De-duplicated shared render-uniform `extern` declarations onto the new
  `gfx_uniforms.h`, and repaired stale documentation references to the removed
  legacy `gfx_pc.c`.

## Released

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for full notes.

- **v0.4.0-alpha.3** — faithful full-auto cadence, rumble, solo pause, handheld/controller UX (pre-release)
- **v0.4.0-alpha.2** — controller-first navigation, Windows-native builds, in-app update check (pre-release)
- **v0.4.0-alpha.1** — weapon/pickup fixes, intro fidelity, rendering & input hardening (pre-release)
- **v0.3.2** — repository-quality pass + Fast3D license correction
- **v0.3.1** — Remote Mine detonator fix
- **v0.3.0** — MGB64 becomes a real in-process app (launcher + in-game overlay)
- **v0.2.1** — pre-ship hardening
- **v0.2.0** — visual remaster + native Metal backend
- **v0.1.0** — first public release
