# Release Notes

## v0.4.0 — the faithful 0.4 release

> **Stable release.** v0.4.0 promotes the v0.4.0-alpha series to the actual
> "Latest" build. Everything from the alphas (below) ships here, plus a final
> faithfulness and release-hygiene pass. Still bring-your-own-ROM. The macOS
> `.app` is unsigned — on first launch, **right-click → Open** to pass
> Gatekeeper (Apple notarization is deferred).

- 🎯 **The faithfulness sweep lands.** A large batch of accuracy fixes validated
  against a stock-ROM reference — authentic weapon and guard fire cadence, the
  Automatic Shotgun behaving as a shotgun, remote-mine detonation and correct
  ammo crates, watch/HUD tint/scroll/aspect corrections, a radial movement
  deadzone, the Silo sky-leak sealed, and clean audio at uncapped frame rates,
  among others.
- 🕹️ **Everything from the v0.4.0 alphas.** Authentic full-auto fire on by
  default, Rumble, real single-player pause, keyboard-free ROM selection,
  friendlier settings, a bundled controller database, handheld readability, and
  a PortMaster/GLES build target. See the alpha notes below for detail.
- 🧹 **Release hygiene.** Ejected shell casings use a well-defined color path,
  the image-diff regression checks skip cleanly without Pillow, and the public
  docs (prebuilt availability, macOS build/arch, internal-doc boundaries) are
  brought current.

## v0.4.0-alpha.3 — faithful fire, rumble, solo pause, handheld polish (pre-release)

> **Pre-release.** The v0.4.0 alpha series is an in-progress preview on the road
> to v0.4.0, published for testing. Still bring-your-own-ROM; the
> automation/validation path is unchanged.

- 🔫 **Authentic full-auto fire cadence, on by default.** Full-auto weapons now
  fire at the N64's original tick-scaled rate (`Input.FireRateAuthentic`).
- 🎮 **Rumble.** The game's Rumble Pak signal now drives controller vibration
  (`Input.Rumble`, tunable with `Input.RumbleIntensity`).
- ⏸️ **Real single-player pause.** Opening the in-game overlay in solo play now
  freezes the simulation instead of letting it keep running under the menu.
- 📀 **Keyboard-free ROM selection.** The launcher auto-scans for your ROM and
  adds an in-app browser, so you can get in with a controller alone.
- 🎛️ **Friendlier settings.** A dev/diagnostic "advanced" tier keeps the common
  options uncluttered, alongside per-tab "Reset to defaults", inline help, and
  plain-language descriptions.
- 🕹️ **More controllers, more screens.** A bundled controller database is loaded
  before pads are opened, and `UI.Scale` / `UI.LauncherFullscreen` make the UI
  readable on handhelds. A PortMaster/handheld (GLES) build target also landed as
  a community contribution.
- 🎯 **Fidelity fixes.** The Automatic Shotgun fires through its correct path,
  thrown sticky projectiles clamp cleanly on wall hits, glass shards use the
  retail shard scale, the Metal backend honors `Video.MSAA`, and the ammo-total
  HUD restores its retail multiplayer behavior.
- 🧪 **Opt-in visual experiments.** An early, default-off scene-decoration /
  modern-mesh preview for a single level — a work-in-progress showcase, not a
  gameplay change.

## v0.4.0-alpha.2 — controller-first, Windows-native (pre-release)

- 🎮 **Play from the couch.** Full gamepad navigation for the launcher and the
  in-game overlay, rebindable player-1 buttons, names for all 21 SDL controller
  buttons, and rebinds that persist between sessions. The overlay toggles on
  Back/View while Start stays the N64 Start.
- 🪟 **Runs on a stock Windows install.** The Windows `.exe` statically links the
  GCC runtimes (no extra DLLs to chase), launches cleanly as a GUI app with a
  real icon and version info, writes a diagnostic log, and ships with an
  install/setup guide in the portable `.zip`. Crashes now produce real `[CRASH]`
  diagnostics via structured exception handling, and the stall watchdog runs on
  Windows too.
- 🔔 **Update check.** An opt-in `Game.CheckForUpdates` shows your running
  version and a banner when a newer release is available.
- 🎬 **Fewer blank cinematic shots.** The establishing-camera portal walk is
  decoupled from the simulation and on by default, so intro/outro/death-cam shots
  that frame away from Bond render their scenery instead of bare sky. Post-intro
  movement also works correctly at the swirl→first-person handoff.
- 🖼️ **Real app branding** across macOS, Windows, and Linux, and release tooling
  gained Developer ID signing/notarization support.

## v0.4.0-alpha.1 — weapon & pickup fixes, intro fidelity, hardening (pre-release)

- 🔫 **Weapon fixes.** The tank cannon fires one shell per trigger press instead
  of full-auto, and the Cougar Magnum and Grenade Launcher fire-rate floor is
  restored.
- 🎒 **Pickup fixes.** A magazine pickup grants that clip's real ammunition
  (previously it could hand out remote mines), and a multi-ammo crate gives the
  authored quantity per slot.
- 🎬 **Intro fidelity.** Bond's head is no longer overwritten by the intro
  weapon, he plays his scripted intro animation with root motion, and he stays
  grounded through the intro swirl instead of hovering.
- 🔦 **Muzzle flash fixed.** Forced room fog no longer tints the first-person
  muzzle flash.
- 🕹️ **Input feel.** A radial dead-zone and rescale for the movement stick, and
  mouse-wheel weapon cycling that advances exactly one weapon per notch.
- 🛡️ **Stability & rendering hardening.** A simulation stall watchdog (heartbeat
  plus a forensic dump), HUD ammo-icon fallbacks and a health readout,
  `FrameCap=display` paced to 60 Hz so the sim can't run too fast, and a batch of
  renderer robustness fixes (grow-on-demand color-combiner and texture pools,
  capped display-list recursion, guarded allocators, native Metal minimap).

## v0.3.2 — repository quality, Windows crash fix, license correction

- 🪟 **Windows: no more crash on load.** MinGW/GCC defaulted to a struct-packing
  mode that repacked the game's N64-layout structures and desynced them from the
  ROM data read through them, crashing on level load. The engine now builds with
  the matching layout on every platform (byte-identical on macOS/Linux), so
  Windows parses the data correctly.
- 🔫 **AR33 (M16) and RC-P90 fire full-auto again.** Both are full-auto on the
  N64 original, but the port had grouped them with the semi-auto pistols; they
  are now routed to the machine-gun firing group the retail game assigns them to.
- 📄 **Licensing correctness.** The vendored Fast3D renderer's upstream license
  was corrected from MIT to its actual modified BSD-2-Clause (© 2020 Emill,
  MaikelChan) and reconciled verbatim against upstream; the port's prebuilt
  binaries are asset-free and satisfy its binary-redistribution clause.
- 🧹 **Repo quality.** Navigable docs/tools indexes, a generated drift-gated
  environment-flag reference, a one-command local CI runner, a hardened ROM DMA
  bounds check, and new ROM-free test guards — with the internal design/roadmap
  notes moved out of the public tree.

## v0.3.1 — Remote Mine detonator fix

A focused gameplay-correctness patch.

- 💣 **The Remote Mine detonator watch works again.** After throwing Remote
  Mines, switching to the detonator and pressing fire now detonates your live
  mines, exactly as on the N64 original. The port's hand-written first-person
  weapon state machine had dropped the detonator (`ITEM_TRIGGER`) from its firing
  switches, so the watch was inert; it is now routed precisely as the retail
  jump tables specify. Bring-your-own-ROM, and the automation/validation path is
  unchanged (byte-identical with this fix off any other item).

## v0.3.0 — MGB64 is now a real app

MGB64 grows from a CLI into a **desktop app**: a Dear&nbsp;ImGui launcher +
in-game overlay rendered *in-process* (no second window, no webview), built from
**one portable codebase** for macOS, Windows, and Linux. Still bring-your-own-ROM;
the automation/validation path is unchanged (byte-identical).

- 🎮 **Insert-ROM launcher.** A native "Choose ROM…" picker validates your dump
  (region / byte-order / size) before you play, and remembers it next launch.
- ⚙️ **Everything configurable.** A Settings panel auto-generated from the engine
  config schema (all Video / Input / Game / Audio keys, correctly-typed widgets,
  live vs. restart), a **Launch Options** panel to jump straight to any level /
  difficulty / multiplayer, and a **Controls** panel to rebind keys.
- 🎛️ **Modes & Toggles.** One-click Faithful / Faithful-HD / Remaster presets,
  gameplay hatches, and an expert `GE007_*` override box.
- 🐞 **Diagnostics built in.** A live in-app log, one-click **Export Diagnostics**
  (log + config + system info, *no ROM data*), and a **Report a Bug** shortcut.
- ⏸️ **In-game overlay (F1).** Pause into live settings, return to the launcher,
  or quit — rendered over the running game.
- 📦 **Prebuilt downloads.** A double-clickable, self-contained **`MGB64.app`** on
  macOS; Windows (portable `.zip`) and Linux (AppImage) build from the same
  source and via the release CI. See the README **Download** section and
  [docs/APP_ARCHITECTURE.md](docs/APP_ARCHITECTURE.md).

### Rendering

- 💡 **Opt-in per-pixel directional sunlight.** A geometric-normal (screen-space
  `dFdx`) directional light on both the OpenGL and Metal backends, behind
  `Video.PerPixelLight` / `GE007_PERPIXEL_LIGHT` (**default off** — the faithful
  path is byte-identical).

## v0.2.1 — pre-ship hardening

A stability/fidelity pass on top of v0.2.0: a measured faithfulness pass on the
level intro/outro cinematics (below), a rendering-correctness fix, a one-time
config migration, sim-determinism pins for the faithful presets, and a
Windows/Linux/MinGW portability batch.

### Level intro / outro cinematics

A measured faithfulness pass on the animated mission intros and outros,
validated frame-for-frame against a stock-ROM emulator oracle:

- 🎬 **Establishing shots no longer render blank.** The authored fly-in and
  outro/death cameras frame areas away from Bond's spawn; the visibility system
  seeded only from the player's room, so e.g. Dam's iconic dam-wall establishing
  shot rendered as bare sky and water. The scene now renders correctly for the
  intro swirl, the outro pose, and the death cameras. (`GE007_NO_CAMERA_SEED_FIX=1`
  reverts.)
- 🎥 **Faithful cinematic framing.** New `Video.CutsceneFovY` (default 60)
  renders intros/outros at the N64's original vertical FOV regardless of the
  modern gameplay `Video.FovY`, so authored shots are composed as intended.
- 🕹️ **Intro skip no longer aborts on stick drift.** The default is now the
  original staged skip (a face/trigger button advances one stage); the previous
  "any input jumps straight to gameplay" behavior is available via
  `Game.IntroSkipStyle=1`.
- 🎯 **Deterministic intro camera selection matches the original mechanism.**
  Deterministic boots now roll the camera pick like hardware instead of forcing
  the first camera.
- ✅ **Faithful establishing-camera claim corrected.** Contrary to prior notes,
  neither the port nor hardware shows a Bond figure during Dam's early
  establishing camera — verified with side-by-side pixel captures.
- 🧪 **First automated coverage of the intro/outro path.** A stock-ROM camera
  oracle, a parse-digest determinism gate, and render-health smokes now run in
  `ctest` (ROM-gated; skip cleanly without a ROM).

### Fixes

- 📊 **New FPS overlay, on by default.** Live FPS, frame time, and 1%-low
  readout in the top-right corner. Toggle with `Video.FpsOverlay=0` in
  `ge007.ini` or `GE007_FPS_OVERLAY=0`. Automatically hidden in
  deterministic/headless capture sessions.
- 🎯 **Auto-gun tracer beams now tick by default.** Turret tracer beams
  previously drew but never advanced or expired (frozen tracers); they now
  update every tick, matching original N64 behavior
  (`GE007_AUTOGUN_BEAM_TICK=0` restores the old behavior for A/B).
- 🏔️ **Fixed a Dam rendering artifact** — sky-blue shard geometry could slice
  through the road from spawn at wider fields of view. Root cause was an
  overly-broad portal-adjacency check in the room-visibility supplement;
  narrowed to match the stock portal graph (`GE007_VIS_SUPPLEMENT_ANY_ROOM=1`
  restores the old unconditional behavior for A/B).
- 🔊 **One-time audio config migration.** Installs with a saved
  `Audio.MasterVolume=0.7` from before the music/SFX volume-bus split are
  migrated once to the new unity default (`1.0`) — the old value was a
  dead placeholder that is live now and would otherwise silently attenuate
  all audio.

### Determinism

- 🔒 **`--faithful` / `--faithful-hd` now pin sim behavior, not just visuals.**
  Both presets flip several port-added visibility/physics wideners back to
  their stock defaults (each individually overridable via its own `GE007_*`
  env var) and log the resolved state at boot.
- 🎥 **`--deterministic` now also pins FovY**, removing one more source of
  frame-to-frame/run-to-run divergence in deterministic capture and replay.

### Portability

- 🪟 **Windows saves** now go to `%APPDATA%\ge007\` with a real write-probe
  (rather than assuming the directory is writable), fixing save failures on
  locked-down installs.
- 🪟 **MinGW compile fixes** for several blockers that previously stopped a
  from-source Windows build, including a Windows-only field-name collision
  with the C library's `errno` macro (renamed to `errnum` internally).
- 🐧 **Linux build fix** — `-fno-strict-aliasing` added for GCC, resolving
  undefined-behavior-driven miscompiles under `-O3` (this is a decomp port
  with pervasive pointer punning between original struct layouts and native
  types).

### Robustness

- 🛡️ Crash/robustness hardening batch: safer fallback behavior when optional
  OS-level setup (e.g. the crash-handler alternate signal stack) isn't
  available, and a latch fix so a transient graphics-resource failure doesn't
  wrongly stay latched forever once the triggering condition changes.

## v0.2.0 — visual remaster + native Metal backend (release candidate)

**v0.2.0 adds an opt-in visual remaster and a native Metal graphics backend on
macOS**, layered on top of the v0.1.0 faithful port. Everything is behind flags:
the byte-faithful 1997 look is preserved (`--faithful`), and no visual option
changes the gameplay simulation. Still a community-iteration release — bring your
own legally-owned ROM.

### Highlights

- 🎬 **One-switch visual modes.** `--faithful` (byte-faithful original),
  `--faithful-hd` (the faithful look at 2× supersampling, HD-pack-ready), and
  `--remaster` (the full cinematic stack). See
  [docs/VISUAL_MODES.md](docs/VISUAL_MODES.md).
- ✨ **Remaster post-processing** — screen-space ambient occlusion, bloom, filmic
  tonemap, per-level colour grade, FXAA, contrast-adaptive sharpen, vignette,
  output dither, and 2× supersampling. Each effect is an independent
  `Video.*`/`GE007_*` toggle.
- 🖥️ **Native Metal graphics backend** on macOS (`GE007_RENDERER=metal`, selected
  automatically by `--remaster`), at parity with the GL reference path and without
  the Apple GL-over-Metal ambient-occlusion hang.
- 🧵 **Smoother frame pacing** — a sub-millisecond deadline pacer that locks the
  frame interval to the display, fixing periodic judder while turning/strafing.
- 🖼️ **HD texture pipeline** — build HD packs from *your own* ROM dump with
  `tools/texpack` (Real-ESRGAN / procedural synthesis). Packs are ROM-derived and
  are never distributed.
- 🔦 **Opt-in lighting features** (default-off, under review) — smoothed
  environment normals that remove baked per-quad lighting seams
  (`GE007_ENV_SMOOTH_NORMALS=1`) and destructible light fixtures
  (`GE007_SHOOT_OUT_LIGHTS=1`).
- 🕹️ **Modern feel options** (on by default; all reverted by `--faithful`) — a
  modern crosshair, hit markers, a tactical minimap, and widescreen FOV; plus
  opt-in aim-down-sights (`Input.AdsEnabled=1`) and 2–4 player split-screen
  (`--multiplayer --players N`).

### Rails

Every remaster feature is behind a named flag, defaults to a byte-identical
faithful baseline, and never reads or writes the gameplay simulation. All
HD/ROM-derived assets are local-only and never committed.

## v0.1.0 — first public release (community-iteration)

**v0.1.0 is a community-iteration release**, not a 1.0 replacement for the
original game. The goal is to get a genuinely playable, *honestly described*
native port into the community's hands so rendering, audio, and gameplay parity
can be improved together. You must bring your own legally-owned ROM — no game
data is included ([DISCLAIMER.md](DISCLAIMER.md)).

### Highlights

- 🎮 **Plays the whole single-player campaign.** All 20 levels boot, play, and
  reach mission-complete, with working combat, guard AI, weapons, pickups,
  objectives, menus, and audio.
- 🧱 **Native port of a decompilation** — runs the game's decompiled
  C/asm on PC/macOS via an OpenGL renderer + audio synthesis + keyboard/mouse and
  gamepad input.
- 🔒 **Asset-free.** Builds and runs from a clean checkout + *your* ROM; no ROM
  media is committed or compiled into the binary. The release guard
  (`scripts/ci/check_release_ready.sh`) checks for ROM/media contamination and
  is wired into the local release guard and source-archive smoke lane.
- 🖥️ **macOS app shell sources** and a local unsigned `.app` build script are
  included. Locally built app bundles can be verified asset-free; signing and
  notarization remain deferred.
- 🧪 **Documented instrumentation** — a contributor quick-lane
  (`tools/validate_quick.sh`) and [docs/INSTRUMENTATION.md](docs/INSTRUMENTATION.md).
- 📖 **Honest framing** — [PORT.md](PORT.md) documents exactly what is faithful,
  the known divergences, and the validation coverage.

### Known gaps (see [PORT.md](PORT.md) for the full list)

- Rendering/audio use documented compatibility approximations (prop-impact
  decals, room scissoring, sky fallback, a small frontend brightness residual,
  and remaining native music fidelity gaps versus hardware/reference output).
- Authored level intro cameras still differ from hardware/reference output in
  some scenes. Dam's early establishing camera does not show Bond on hardware
  either (measured); the native gap there is that the establishing camera's
  room admission keys off the unspawned player position instead of the
  camera's own position, so distant scenery (e.g. the dam wall/reservoir
  vista) can render as mostly sky.
- The repository still contains inventoried N64 SDK/libultra compatibility
  material that is not MIT-licensed first-party code; see
  [THIRD_PARTY.md](THIRD_PARTY.md).
- Save / mission-flow persistence has deterministic multi-folder EEPROM smoke
  coverage, but organic menu/mission flows are not yet validated end-to-end.
- Some N64-exact timing/AI cadence is still converging (does not affect normal play).
- No packaged prebuilt distributables are attached for this release. Build from
  source; the macOS `.app` can be built locally, but signed/notarized
  distribution is still deferred.
- Linux/GCC full native build plus ROM-free CTest is covered by the local
  preflight/source-archive smoke lane for launch; Linux ROM-backed runtime play
  smoke and Windows/MSYS2 setup are still less exercised than macOS.

### Reporting bugs

Please include: platform, ROM region (U/E/J), level/scene, expected vs. actual,
and steps to reproduce. **Do not attach ROMs, saves, or copyrighted game
imagery.** Parity issues: check [PORT.md](PORT.md) first, and note whether any
`GE007_*` env toggle changes the behavior.
