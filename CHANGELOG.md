## TitanEYE + Ribbon BuildFix22 — pikaVR / platform performance pass

- adds the host-validated pikaVR resource-management layer between TGR and TitanVideo without replacing the TGR architecture or changing correct visual semantics.
- makes VRAM telemetry event-derived, separates 4 MiB GS capacity from the managed texture budget, preserves POINT/LINEAR sampler identity, and counts physical eviction only after backing storage is reclaimed.
- moves exact lossless RGBA32-to-PSM8/CLUT analysis to resource-creation time with safe PSM32 fallback when a texture exceeds 256 exact GS colors.
- adds measurable VlombisCore storage-scheduler evidence and capability-gated technology badges; no technology badge is granted from title branding alone.
- preserves SAS/mSAS metadata priority (`app.cfg` -> `title.cfg` -> folder prefix -> name fallback) while separating software classification from the actual external-ELF execution model.
- fixes Package Installer row spacing/bounds, UTF-8-safe UI truncation, Library title bounds, DEX safe area, full-theme vs SYSTEM context, and complementary HOME/SYSTEM Ribbon crossfade.
- preserves the approved 480i/720p renderer policy, TitanEYE/TGR bridge and Ribbon/VU1 PATH1 implementation.
- extends TitanDiagnostics with filterable pikaVR/SAS/badge/Vlombis categories and keeps new logging out of the frame/draw hot path.
- closes the complete Project Titan host contract suite, including all historical contracts and BuildFix22 additions.
- validation status: HOST VALIDATED. The current environment has no R5900 PS2DEV toolchain, `dvp-as`, PCSX2 or physical PS2 access, so cross-build, PCSX2 and hardware validation are not claimed.

## TitanEYE/TGR v2 source-complete release candidate

- completes the in-process GoldenEye platform replacement with TGR-native
  renderer, audio, input/rumble, storage, timing and cooperative lifecycle.
- adds fixed-function two-texture materials, perspective STQ, fog, alpha,
  depth/blend, viewport/scissor and sampler state to the append-only TGR ABI.
- replaces eager ROM/SFX residency with bounded streaming and LRU caches so the
  port fits the 12 MiB game heap and 8 MiB relocated module-image contract.
- adds the public `sdk/titan-game/` documentation and minimal R5900 ET_REL
  example while keeping ROMs and extracted commercial assets private.
- preserves the boot chain and limits TitanEYE branding to the requested
  GoldenEye logo branch.

## RT1 BuildFix17 — persistent Network Settings

- fixes the BF16 `network.cfg` replacement bug that saved the SSID but silently lost the following WPA2 PMK update.
- publishes Network Settings transactionally with `network.tmp` + `network.bak`, flushes before replace and recovers from the backup on boot.
- persists Internet enable state, preferred interface, SSID and derived WPA2 PMK without storing the plaintext password.
- retries settings load during early boot and automatically reconnects the saved Wi-Fi through the existing TT1426 path.
- preserves BF16 Asset TCP/session behavior, AR9271 IOP code and the frozen bootloader.

## RT1 BuildFix16 — asset TCP lifecycle/session drain

- replaces per-request TCP churn with a two-slot HTTPS keep-alive pool and four-entry DNS cache for long asset sessions.
- drains framed non-200 bodies for safe connection reuse, retries stale pooled connections once, and self-heals stale DNS after repeated connect failures.
- adds socket/connect/reuse/close diagnostics and makes manual cancellation wait for the live network/storage pipeline to drain before `ASSET_SESSION_END`.
- preserves ICON/BACKGROUND semantics, decoder/storage hardening, Network Core, AR9271 path and frozen bootloader.

## RT1 BuildFix15 — asset transport state & recovery

- removes stage-blind network stall cancellation, adds structured HTTP failure state, 60s request deadlines, semantic CDN/raw mirrors, complete-body validation and Adam7 PNG support.
- keeps ICON strictly square/icon artwork and BACKGROUND strictly landscape art; third-party bad/missing content is not reported as a Titan transport failure.
- preserves BF14 full-frame output, notification layout, DEX IP, Network Core and frozen BF6 bootloader.
- fixes premature manual-session completion by tracking the actual in-flight network kind and waiting for the entire network/storage pipeline to drain before session end.
- makes storage-stall cleanup owner-safe; the main thread requests cancellation and the serialized storage worker owns cleanup/requeue.

## RT1 BuildFix12 — asset UX, recovery and artwork quality

- upgrades game-icon TIMG from 32x32 to 128x128, rejects undersized remote art and fills Home game tiles with downloaded artwork while preserving the existing 720p VRAM layout.
- makes DEX/Title ID metadata an explicit overlay layer above game art and keeps the focus frame last.
- fixes false asset-worker busy states with queued starts, bounded retry, cache-truth resume, catalog rebind by startup hash and guarded worker recreation.
- enforces icons-first ordering for manual and automatic passes and retains selected/visible games ahead of the remaining catalog.
- adds dedicated asset-download progress to the top notification ticker and Notifications > Downloads.
- synchronizes the DEX overlay IP with Titan Network Core without modifying `titan_network.c`.
- preserves the frozen BF6 bootloader and all unrelated subsystem behavior.

## RT1 BuildFix3 — PS4-style Network Settings / automatic AR9271 scan

- starts the resident TT1426 passive scanner automatically when an AR9271 is detected, even with no stored SSID/PMK.
- exposes the TT1426 AP cache (SSID/RSSI/channel/security) to the EE Network Core and Settings UI.
- replaces manual SSID/password rows with the PS4-style Network hierarchy from the supplied reference video; manual SSID is now only `Outra rede...`.
- adds persistent Connect to Internet enable/disable, connection-test/status/service pages and Easy/Custom setup branches for Wi-Fi/RJ45.
- preserves BuildFix2 deferred network boot and the official BF6 bootloader unchanged.

## Build 36 — Complex Theme API v3 / idle scene loops

- advances the supported Theme Shell API to v3 while retaining API v1 Flow and API v2 scene compatibility.
- adds bounded HOME and TOP idle loops to `scene-v2`, with monotonic-ms timing and `loop`/`pingpong` playback.
- pauses/resets idle loops during HOME<->TOP transitions so existing reversible transition keyframes remain synchronized to the shell.
- caps additional HOME+TOP idle frames at eight, keeping the maximum scene cache at 4 MiB EE RAM with six transition keyframes and no new persistent GS VRAM reservation.
- hardens `.tthm` build/verify by checking manifest metadata and requiring `theme.cfg id` to match the TPKG title ID.
- advances system updater generation to Build 36 and self-test generation to 37 while preserving the seven-entry hardware bootstrap envelope.
- adds the community-facing `docs/THEME_AUTHORING.md` reference.

## Build 35 bootstrap correction — kernel-only PUP

- keeps the Theme Shell API v2 implementation and system build number at 35;
- changes only the BuildFix34 -> Build35 PUP recipe to publish `kernel.elf` and nothing else;
- removes unrelated wLaunchELF and built-in Flow-theme payloads from the Build35 update transaction;
- preserves forward-only build checks, table CRC and payload CRC verification.

## Build 35 — Dynamic Theme API v2

- adds `scene-v2` installable `.tthm` themes with 512x256 RGB565 HOME/TOP keyframes and up to six transition frames driven by the shell's real top-menu transition.
- scene themes render no ribbon on HOME or the top menu; Flow is restricted to Settings, Profile, Saved Data/VMC, Library and fullscreen folder-option screens.
- adds per-theme background music and SFX overrides, including a dedicated top-menu-enter event for transformation/effect sounds; missing assets fall back to Titan defaults.
- adds declarative global icon tint plus ring/corner decorators without allowing executable theme payloads.
- keeps Theme API v1/Flow themes compatible while advancing the supported shell API to v2.
- system updater generation and PUP metadata advance to Build 35.

## BuildFix34 — head-driven snake Flow

- replaces Scarlet Flow stochastic whole-sheet wander with a head-driven trailing spine.
- head moves through smooth Catmull-Rom pseudo-random waypoints; body samples historical head positions so turns propagate from head to tail.
- ribbon orientation is now derived from the local trajectory tangent rather than a global rotation transform.
- preserves VU1 EFU/ESIN fine deformation but lowers its amplitude/speed so it adds surface life without making the entire ribbon wobble.
- Scarlet Flow advances to 6.0.0; updater generation advances to build 34.

## BuildFix32 — stochastic PS4 Flow wander

- removed Scarlet Flow's closed orbital sweep; the reference video shows no persistent trajectory.
- added theme-owned stochastic wander states for center position, orientation, curvature, width, thickness and visibility.
- transitions use deterministic hashing plus quintic smootherstep, so direction changes are occasional and organic without teleporting or requiring mutable RNG state.
- dynamic alpha lets the composite ribbon become faint or nearly disappear between appearances, like the PS4 reference.
- retained VU1 EFU/ESIN fine deformation, but reduced Scarlet Flow carrier amplitude/speed so it deforms instead of 'wiggling around the corners'.
- advanced PUP generation to build 32.

## BuildFix31 — VU1 procedural Flow / EFU offload

- moved the fine per-point Flow deformation carrier from EE LUT sampling into the VU1 microprogram.
- VU1 now evaluates two folded sine carriers per point with EFU `ESIN`, synchronizes with `WAITP`, and applies X/Y displacement before rail extrusion and `XGKICK`.
- ESIN inputs are quadrant-folded to the documented 0..pi/2 safe range; quadrant polarity is encoded in signed amplitudes.
- Theme Engine now exposes `vu1_deform_speed_milli`, `vu1_deform_frequency_milli` and `vu1_deform_amplitude_milli`.
- Scarlet Flow advances to 4.0.0 and uses the VU1 procedural carrier while EE retains the slow orbit/Bezier/Gerstner spine and software fallback.
- system update generation advanced to build 31.

## BuildFix30 — continuous-phase Flow / spline morph

- fixed the root cause of the Flow "PowerPoint" motion: slow animation phases were truncated to integer LUT entries, so the ribbon could hold for dozens of frames and then jump while the shell itself remained smooth.
- added fractional interpolation over the existing 128-entry wave LUT; no per-frame sin/cos or libm cost is introduced.
- converted Flow sweep, breathing, visibility, drift, Gerstner carriers, noise, flex, span pulse and local rotation to continuous fractional phase.
- added slow independent morphing of both cubic-Bezier control points so the ribbon's global curvature evolves over time instead of rotating a mostly fixed spine.
- Scarlet Flow advanced to 3.2.0; PUP generation advanced to build 30.

## BuildFix29 — organic Flow motion / visibility pass

- reworked Flow ribbon motion so it no longer reads as a rigid strip rotating like a slideshow.
- added slow ribbon breathing, per-point flex, non-uniform span pulsing and width taper so the ribbon deforms organically.
- improved ribbon visibility and dynamics by increasing Scarlet Flow alpha, thickness, amplitude, drift and local noise in a controlled way.
- kept the single-ribbon PS4-inspired behavior from BuildFix28, but made the motion feel alive rather than mechanically rotated.
- advanced PUP generation to build 29.

## BuildFix28 — reference-video Flow sweep

- Replaced Scarlet Flow's two-crossing-wave approximation with one composite 7-strip ribbon based on the supplied PS4 Flow reference video.
- Added per-primary-wave long-cycle orbital sweep controls (speed, X/Y orbit, span, phase) while preserving support for up to three main waves in future themes.
- Swept ribbons rotate tangent to an elliptical orbit, allowing one continuous sheet to become vertical/diagonal/horizontal and enter/leave any screen edge.
- Scarlet Flow advanced to 3.0.0 with a ~97 s sweep at 60 Hz and significantly reduced local noise/deformation.
- System update generation advanced to 28; the real PUP continues to include the wLaunchELF test payload.

# Changelog

## 0.3.90-rc14 — Titan App containment / Project Nes / VU1 BuildFix12

- Audits the active Titan App boundary after physical-PS2 graphics corruption that began only after `titan_app_init` completed.
- Removes the global renderer transform from foreign module execution and validates/maps every Titan App UI/media primitive into the shell-owned app viewport.
- Adds bounded per-app allocation ownership, automatic session cleanup and buffer-span validation for file/audio/video APIs.
- Moves Project Nes 0.1.2 state/frame/audio buffers out of ET_REL `.bss` into Titan-owned heap allocations; the host MIPS audit `.bss` falls from ~408 KiB to 4 bytes.
- Adds guarded full-lifecycle Project Nes tests plus ASan/UBSan and analyzer coverage.
- Hard-synchronizes PATH3 -> PATH1 with GS FINISH before VU1 XGKICK, increases VU1 wait budgets and tolerates isolated transient stalls before permanent fallback.
- Preserves all curated assets byte-for-byte. Real PS2 validation remains required.

## 0.3.90-rc14 — Package single-owner / RAM preload BuildFix9

- Moves Package Installer filesystem RPCs back to the main EE thread while package-exclusive mode parks the background storage worker.
- Adds an explicit worker-park acknowledgement so package I/O cannot begin while the storage worker is still inside another filesystem client.
- Reuses the header/file-table plan already validated during Package Installer enumeration instead of repeating stat/header/table reads after Install is pressed.
- Preloads payloads up to 1 MiB into aligned EE RAM, closes the source `.pkg`, then performs destination-only staging writes; larger packages keep the 4 KiB streaming fallback.
- Preserves generation-based transactional staging, request-only cancellation, no per-file fsync, and suspension of scanner/VMC/assets/music during the exclusive installer session.
- Full host test suite and host syntax checks pass; real PS2 hardware validation remains required.

## 0.3.90-rc14 — Package Exclusive I/O / Audio Cushion BuildFix6

- Gives Package Installer exclusive ownership of the storage worker from install start until the user exits the installer UI; an in-progress catalog scan pauses instead of blocking installation.
- Removes staged-file/manifest `fsync()` calls and changes payload extraction to one persistent partial `read()` or `write()` per scheduler turn.
- Keeps Circle request-only but exposes `Cancelamento solicitado...` immediately while the worker reaches the next I/O boundary.
- Adds in-memory progress diagnostics for open/read/write/close boundaries without adding debug storage traffic.
- Fades Magenta Melody out for installation and freezes buffered playback after the fade, resuming from the retained position only after leaving Package Installer.
- Expands the decoded music ring to 1 MiB with 768 KiB/896 KiB refill thresholds and reduces scan-step delay from 1.5 ms to 0.25 ms.
- Defers post-install catalog rebuild until leaving Package Installer and preserves all approved UI/audio assets.
- Host tests pass with package storage artificially limited to 3-byte partial transfers. Real PS2 hardware validation is still required.

## 0.3.90-rc14 Titan App SDK / Package Installer extension

- Adds TPKG v2 and the Titan App API v1 for in-process `.tmod` applications.
- Package Installer remains DEX-only and now exposes `.pkg` filenames plus a dedicated installation-progress screen.
- Installed apps can provide a tile icon, metadata and optional RGB565 background.
- Adds Home context-menu uninstall with background storage deletion and catalog refresh.
- Makes the DEX performance overlay independently persistent from DEX Mode.
- Adds kernel PCM/RGB565 media-output services for community media-player ports without allocating additional 720p VRAM.
- Preserves TPKG v1 external-ELF compatibility and all existing UI assets.


## 0.3.90-rc14 — Titan UL hardware handoff hotfix 2

- Passes the already-scanned USBExtreme title, part count, media type and
  source path from the shell to Titan UL Engine instead of re-reading `ul.cfg`
  after the IOP reset.
- Uses bounded/cooperative IOP reset and USB re-enumeration waits, and loads BDM
  only after its I/O worker/semaphore are initialized.
- Fixes the headless VMC override being cleared by `setDefaults()` before
  launch, and continues without VMC if its physical cluster chain cannot be
  mapped for mcemu.
- Removes pre-GUI message-box failure paths from Titan headless BDM launch and
  validates the `sbPrepare` patch index before pointer arithmetic.
- Returns to the PS2 Browser with an explicit error code if Titan UL cannot
  complete the handoff, instead of leaving the console in a black-screen hang.
- Preserves every file under `kernel/assets/ui` byte-for-byte.

## 0.3.90-rc14 — Home grid, UL catalog and APP_HOME correction

- Keeps collapsed Home tiles above the footer while the system menu is open.
- Fits every footer hint to the 640-pixel logical viewport instead of clipping
  long control strings at either edge.
- Deduplicates USBExtreme entries by startup ID rather than by the shared
  `ul.cfg` path, allowing every valid UL record to reach the Home catalog.
- Scans the `mass0:` first-device alias even when `mass:` already yielded ISO
  games, without duplicating alias-equivalent ISO paths.
- Gives UL folder members stable startup-based keys and reads legacy RC13
  `ul.cfg` path keys without grouping every UL title together.
- Stops unresolved folder keys from drawing fake placeholder artwork.
- Replaces only the owner-authorized APP_HOME artwork with a transparent,
  optically centered 128x128 alpha mask and routes the tile to that asset.
- Preserves RC13 build fixes, RC11 audio/music, 720p, profiles, DEX, Package
  Installer and every visual asset other than APP_HOME.
- Updates kernel metadata to `0.3.90-rc14`, UI metadata to `0.9.34`, and
  bootloader target metadata to `0.1.14-rc14`.

## 0.3.90-rc13 — Titan UL SMSTCPIP parallel rebuild correction

- Replaces the Titan UL `SMSTCPIP` recursive `rebuild` invocation with `all`.
  The disposable module tree is already cleaned before the parallel build, so
  invoking `rebuild: clean all` was both redundant and unsafe.
- Prevents GNU Make's inherited parallel jobserver from running the `clean` and
  `all` prerequisites simultaneously and deleting `SMSTCPIP` objects while
  they are being compiled or linked.
- Adds a host source contract that rejects any return to the parallel-unsafe
  `SMSTCPIP` rebuild path.
- Preserves the RC12 `cdvdman` ordering, RC11 audio/music/Title ID behavior,
  RC10 packaging fix, 720p centring, USB/profile/DEX/catalog recovery, Package
  Installer and every approved visual asset.
- Updates kernel metadata to `0.3.90-rc13`, UI metadata to `0.9.33`, and
  bootloader target metadata to `0.1.13-rc13`.

## 0.3.90-rc12 — Titan UL cdvdman parallel-build correction

- Serializes only the five Titan UL `cdvdman` variants whose nominally
  variant-local object paths normalize to the shared `isofs/zso.o` and
  `isofs/lz4.o` files.
- Prevents a recursive linker from reading either shared object while another
  variant is still replacing it under `make -j`.
- Groups recursive Titan UL output per submake so a future fatal diagnostic is
  kept together with its failing target instead of being buried in interleaved
  parallel output.
- Adds a host source contract for the complete BDM -> BDM-ATA -> SMB -> HDD ->
  HDPro ordering chain while preserving parallelism for unrelated modules.
- Preserves the complete RC11 audio/music/Title ID implementation, RC10
  packaging fix, 720p centring, USB/profile/DEX/catalog recovery, Package
  Installer and every approved visual asset.
- Updates kernel metadata to `0.3.90-rc12`, UI metadata to `0.9.32`, and
  bootloader target metadata to `0.1.12-rc12`.

## 0.3.90-rc11 — navigation audio, menu music and DEX metadata alignment

- Corrects the `audsrv_set_volume()` value from percentage-style `100` to the
  PS2SDK/SPU2 `MAX_VOLUME` scale; the existing per-voice mixer continues to set
  the audible balance between menu music and system effects.
- Routes folder-grid, folder editor/context and package/interface navigation to
  the appropriate SFX, while preventing the shell-move path from issuing a
  redundant generic navigation event.
- Adds bounded 32 kHz stereo IMA-ADPCM decoding to the existing cooperative
  music ring buffer; PCM SFX and legacy PCM menu music remain supported.
- Includes the owner-supplied Retrovex Ambient track from exactly `01:30.000`
  through its remaining duration and starts it when the health screen appears.
- Recomputes DEX metadata width after scale-to-fit and horizontally centers the
  Title ID/version strip on Home and folder tiles.
- Preserves RC10 720p centring, storage/profile/DEX recovery, game catalogue,
  Package Installer, engine packaging and all 241 approved visual assets.
- Updates kernel metadata to `0.3.90-rc11`, UI metadata to `0.9.31`, and
  bootloader target metadata to `0.1.11-rc11`.

## 0.3.90-rc10 — Titan Neutrino runtime packaging correction

- Moves the Titan Neutrino Engine `copy` target after the final EE loader
  `clean all`, because that clean target intentionally removes
  `ee/loader/modules`.
- Validates `neutrino.elf`, `config`, the generated `modules` directory and its
  IRX payload before publishing the disposable runtime package.
- Adds a host source contract that rejects the destructive RC9 build order.
- Preserves RC9 physical 720p centring, USB profile/DEX/catalog recovery, Home
  game visibility and all approved assets without runtime behavior changes.
- Updates kernel metadata to `0.3.90-rc10`, UI metadata to `0.9.30`, and
  bootloader target metadata to `0.1.10-rc10`.

## 0.3.90-rc9 — physical 720p centring and USB state recovery

- Moves only the 720p CRTC display window from the generic PS2SDK defaults to
  the hardware-centred coordinates already used by the integrated Titan
  UL/GSM timing, preserving the RC3 native double-buffer/VRAM correction.
- Makes the IOP USB bootstrap attempt IOMANX, FileXio, USBD and USBHDFSD
  independently instead of abandoning the remaining chain after one resident
  module reports an error.
- Treats a real mounted-device probe as authoritative even when a client or
  module reports an already-bound condition.
- Adds transparent `mass:/` to `mass0:/` fallback at the shared transactional
  storage boundary, restoring profiles, VMC, DEX settings, library cache,
  folder layout and installed-package publication without changing on-disk
  paths.
- Limits direct Home application tiles to apps under `TITAN/apps`; generic ELF
  discovery remains available through Applications and can no longer consume
  every dynamic slot reserved for games.
- Adds host regressions for storage alias fallback, transactional publication,
  installed-app Home filtering and the corrected 720p display window.
- Updates kernel metadata to `0.3.90-rc9`, UI metadata to `0.9.29`, and
  bootloader target metadata to `0.1.9-rc9`.

## 0.3.90-rc8 — Debug Settings and USB package installer

- Adds `★debug settings` to Settings with eight working actions: package
  installation, DEX/performance overlay control, Home catalog reload, cache
  clearing, USB debug-report export, APP_HOME, FTP_HOME and controller
  diagnostics.
- Adds the bounded, frame-cooperative TPKG v1 installer with header/table/file
  CRC32 checks, strict relative paths, MIPS ELF validation, cancellation and
  transactional publication from `.install-TITLEID` to the final app folder.
- Stores installed application files and manifests in `mass:/TITAN/apps` and
  publishes validated applications as launchable Home tiles.
- Adds a host-side `.pkg` builder/verifier and host coverage for successful,
  corrupt, unsafe, duplicate and cancelled installations.
- Adds U+2605 to the final unused font-atlas cell, keeps the existing ASCII
  metrics/kerning table intact, and gives both DEX tiles the `★` title prefix.
- Adds the supplied Debug Settings symbol as a transparent white mask in the
  final unused system-icon atlas cell without replacing existing artwork.
- Updates kernel metadata to `0.3.90-rc8`, UI metadata to `0.9.28`, and
  bootloader target metadata to `0.1.8-rc8`.

## 0.3.90-rc7 — Titan UL language dependency graph correction

- Corrects the incomplete RC6 dependency: GNU Make may update prerequisites of
  one target in parallel even when one prerequisite is listed first.
- Makes each translation YAML target and the English template directly
  order-dependent on completion of `download_lng`.
- Preserves parallel compilation after the language checkout has completed.
- Strengthens the source contract to check the real file-level dependency
  edges instead of the ineffective aggregate `languages` edge.
- Preserves the RC3 native 720p renderer, RC4 Git-optional build path, RC5
  Neutrino correction, interface, and all visual/audio assets unchanged.
- Updates kernel metadata to `0.3.90-rc7`, UI metadata to `0.9.27`, and
  bootloader target metadata to `0.1.7-rc7`.

## 0.3.90-rc6 — Titan UL language build ordering correction

- Makes Titan UL Engine language generation depend explicitly on completion of
  the `lng_src` checkout.
- Prevents `make -j` from invoking `lang_compiler.py` against partially cloned
  or not-yet-created translation YAML files.
- Adds a source contract that rejects future packages which remove this build
  dependency.
- Preserves the RC3 native 720p renderer, the RC4 Git-optional build path, the
  RC5 Neutrino `DelayThread()` correction, and all visual/audio assets unchanged.
- Updates kernel metadata to `0.3.90-rc6`, UI metadata to `0.9.26`, and
  bootloader target metadata to `0.1.6-rc6`.

## 0.3.90-rc5 — Titan Neutrino EE loader compile correction

- Replaced the incorrect `<kernel.h>` include used for `DelayThread()` in the
  Titan Neutrino Engine FHI retry path with PS2SDK's `<delaythread.h>`.
- Preserved the bounded 50-attempt, 10 ms FHI open retry without changing its
  runtime behavior.
- Added a source contract that rejects future packages missing the EE
  `DelayThread()` declaration.
- Preserves the RC3 native 720p renderer, the RC4 Git-optional build path, and
  all curated visual/audio assets unchanged.
- Updates kernel metadata to `0.3.90-rc5`, UI metadata to `0.9.25`, and
  bootloader target metadata to `0.1.5-rc5`.

## 0.3.90-rc4 — release ZIP build correction

- Made `scripts/build_all.sh` create its clean disposable source tree from
  either the exact project Git checkout or an extracted release ZIP.
- Removed the invalid `git archive | tar` path that failed when `.git` was
  intentionally absent from the distributed source package.
- Regenerates production headers before each build and keeps the two
  reproducibility passes isolated from previous outputs.
- Pins archive-mode Neutrino and Titan UL Engine version metadata without
  requiring nested Git databases.
- Made source repackaging deterministic outside Git and silenced the harmless
  `git ls-files` diagnostic in the source-hardening host test.
- Preserves the RC3 720p renderer and all curated visual/audio assets unchanged.
- Updates kernel metadata to `0.3.90-rc4`, UI metadata to `0.9.24`, and
  bootloader target metadata to `0.1.4-rc4`.

## 0.3.90-rc3 — native 720p page-flip correction

- Replaced the native 720p single scanout/render surface with two page-aligned
  1280x768 PSM16 storage surfaces displaying 1280x720 each.
- Presents only a completed back buffer at VSync, eliminating the horizontal
  old-frame/new-frame seam caused by drawing into the active scanout surface.
- Rebuilds and reuploads the complete VRAM layout during output-mode changes.
- Preserves the exact font and UI alpha masks as PSM8 indices with one shared
  RGBA32 CLUT, and streams original PSM16 avatars through a bounded scratch
  texture so both native buffers fit in the GS's 4 MiB VRAM.
- Applies logical output mapping from the requested mode before committing the
  CRTC change, fixing the stale legacy mapping used on entry to 720p.
- Updates kernel metadata to `0.3.90-rc3` and UI metadata to `0.9.23`.

## Development after 0.3.90-rc2 — system-tile contextual options

- Enabled `SELECT` contextual menus for Home system tiles and the top system menu.
- Added per-tile actions for Applications, Storage, Controller, Notifications, Profile, VMC Utility, Settings and Power.
- Added persistent manual ordering for Home system tiles and the top system menu through transactional `SHELLLAY.DAT`.
- Added safe hide/restore support for the Applications tile and layout export/import through `SHELLLAY.EXP`.
- Added controller-port reassignment, profile rename, notification clear/read actions and direct VMC/power shortcuts.
- Allowed Applications and Controller to participate in Home folders using compact 64-bit shell keys without duplicating assets.
- Added host coverage for shell layout persistence, export replacement, import and corrupt-primary recovery.

## Development after 0.3.90-rc2 — OrbisOS Home folders

- Added `SELECT` as the PS2 Home contextual-menu equivalent of PS4 `OPTIONS`.
- Added the OrbisOS-style Add to Folder / new-folder / content-selection flow.
- Added folder tiles with 2x2 mosaic presentation and expandable content rows.
- Added edit/delete folder actions without deleting underlying games.
- Added transactional `HOMEFOLD.DAT` persistence with last-known-good recovery.
- Kept engine-specific wording explicit: Titan-Neutrino Engine and Titan UL Engine.
- Added host tests for folder persistence, ownership rules and corrupt-primary recovery.

## 0.3.90-rc2 — source-hardening candidate

### Runtime safety

- Restored canonical PATH3 GS state after VU1 PATH1 rendering.
- Replaced point-by-point main icons with a texture-atlas path.
- Preserved logical aspect ratio in 720p and made mode changes rollback-safe.
- Added checked VRAM arithmetic and explicit allocation ownership.
- Added monotonic fixed-step UI timing and priority-aware notifications.
- Added strict UTF-8 sanitization and explicit visual-asset states.

### Storage and launch

- Made profile, VMC, DEX and library persistence transactional and recoverable.
- Added exact wide I/O helpers and explicit library-cache serialization.
- Added one shared strict ELF validator for bootloader, stage2 and applications.
- Routed applications through validated low-memory stage2 handoff.
- Hardened ISO metadata parsing, catalog records and UL fragment reads.
- Fixed Titan Neutrino Engine FHI allocation overlap and rollback behavior.
- Added explicit active-profile VMC handoff to Titan UL Engine.
- Standardized deployment on `/kernel.elf` and `TITAN/titan_ul_engine.elf`.

### Services and audio

- Moved IOP reset/sync ownership into a bounded service manager.
- Decoupled controller failure from storage, catalog, DEX and audio services.
- Added retryable audio initialization, bounded streaming, partial-enqueue
  preservation, strict WAV parsing and clean handoff shutdown.
- Added six first-party Project Titan system effects; background music remains
  optional and is not redistributed.

### Source and release engineering

- Consolidated the source under the single Project Titan Magenta Edition tree.
- Removed tracked local binaries, generated headers, backup attempts and
  duplicate runtime copies.
- Added deterministic first-party system icons and 199 procedural avatars.
- Added clean temporary builds, reproducibility verification, source packaging,
  transactional USB installation with recovery journals and a fault matrix.
- Added toolchain, architecture, lifecycle, disk-format, compatibility,
  provenance and derived-module documentation.

### Validation boundary

All host suites and source contracts pass. This entry does not claim that the
RC2 MIPS binaries have been built or tested on physical PS2 hardware.

## 0.3.90-rc14 Storage Fairness / Package Cancel BuildFix5
- Fixed Package Installer starvation caused by realtime music prefetch monopolizing the storage worker.
- Package cancel is now asynchronous/request-only on the UI thread; cleanup remains on the storage RPC lane.
- Package preparation moved off the UI thread and extraction quantum reduced to 4 KiB.
- Added 64 KiB audio low-water scheduling and fair one-step bulk progress.
- Reduced library-scan worker throttle from 12 ms to 1.5 ms without changing scan semantics.
- No curated UI assets changed.

## RC14 BuildFix7 — live source snapshot correctness
- Fixed `scripts/build_all.sh` using `git archive HEAD` when the release tree was placed inside an existing Git checkout.
- The disposable build tree now always snapshots the current source tree, so modified/untracked hotfix files are not silently omitted.
- Fixes `test_release_contract.py` failing because `bootloader/include/titan_boot_version.h` vanished from `/tmp/titan-build.*`.
- Applied the same live-tree snapshot rule to `scripts/package_source.sh` so source packages cannot silently omit hotfix files from a dirty checkout.

## RC14 BuildFix8 — resource-managed Package Installer preparation
- Replaced monolithic pre-extraction setup with a cooperative PREPARING state machine.
- Package stat/open/header/table/destination/stage operations now execute as separate storage-worker dispatches with visible phase messages.
- Removed recursive stale-stage deletion from the interactive Package Installer critical path.
- Added generation-based private staging (`.install-<TITLEID>-XXXXXXXX`) so interrupted installs never block the next attempt.
- Preserved package-exclusive storage ownership and suspended menu music during the complete installer session.
- Added host coverage for stale staging plus 3-byte partial-transfer preparation/extraction.

## RC14 BuildFix9 — Package single-owner / RAM preload
- Moved Package Installer filesystem execution back to the main EE owner while the background storage worker is parked.
- Cached validated TPKG plans and preloaded packages up to 1 MiB into EE RAM before extraction.
- Fixed the physical-PS2 regression where package installation remained at 0%/Preparing.

## RC14 BuildFix10 — Titan App R5900 runtime recovery / Project Nes 0.1.1
- Made R5900 machine targeting part of the Titan-native runtime contract; generic MIPS32/MIPS32r2 `.tmod` files are no longer executed in-process.
- Package Installer now performs runtime allocation/relocation validation before publishing Titan-native apps.
- Hardened R_MIPS relocation-table validation and executable-export validation.
- Reworked Titan App launch into phases with single-owner storage, a dedicated init thread, init-time Titan API bridging, a 15-second watchdog and Select+Circle cancellation/recovery.
- Integrated Project Nes 0.1.1 source and deferred optional external-ROM probing out of `titan_app_init`.
- `scripts/build_all.sh` now builds a fresh `PROJECTNES.pkg` using the user's `mips64r5900el-ps2-elf-gcc` and includes it in `TITAN/packages/`.
- Project Nes hardware packaging refuses non-PS2 compilers and explicitly uses `-march=r5900 -mabi=32`.

## RC14 BuildFix11 — Titan App window / RGB565 GS fix
- Fixed the first physical-PS2 Titan App graphics corruption: API RGB565 pixels were being uploaded directly as GS PSMCT16 (R5G5B5A1), corrupting channels and alpha.
- Added RGB565 -> opaque GS PSMCT16 conversion to both legacy/480i media uploads and native-720p streamed tiles; software renderer now mirrors the same semantics.
- Titan Apps are windowed by default over a frozen Home/live-area backdrop; R3 toggles window/fullscreen without an ABI bump.
- Reserved R3 as Titan-owned app chrome input; Select+Circle remains the kernel escape hatch.
- Audited Project Nes media production: 512x256/stride-512 RGB565 presentation is valid and no app-local GS workaround is required.
- Added a host contract covering app window composition and RGB565 known-color conversion.
- Curated UI assets remain byte-for-byte identical to BuildFix10.

## RC14 BuildFix14 — Titan App R5900 ISA guard / Project Nes 0.1.4

- Reject R5900-incompatible COP1 code before a Titan-native module can execute.
- Project Nes hardware build is explicitly single-float and includes a final `.tmod` ISA audit.
- Project Nes ROM start now warms up PPU/video before first present and uses fixed-step 60 Hz audio/video timing.
- No UI asset changes.

## RC14 BuildFix16 — kernel-resident Internet system app
- Ported the existing Titan Browser POC into the current shell as a kernel-resident installed system app.
- Added TPKG v2 `system-browser` packages with reserved `@system-browser` logical entrypoint and no ELF/tmod payload.
- Internet launches inside the running Titan shell and never uses ExecPS2 or the protected Titan `.tmod` context.
- Restored lightweight HTML/link parsing, history, URL input and PS2IP HTTP transport; HTTPS continues through the Titan Stream Bridge architecture.
- Browser network worker is created lazily on the first request.
- External stream-player handoff and download UI are deliberately disabled until in-system media/storage ownership is hardware-proven.
- Added `system-browser-contract` and TPKG positive/negative regression coverage.
- `kernel/assets` and `bootloader/assets` remain byte-for-byte unchanged from BuildFix15.

## RC14 BuildFix17 — Theme Engine / Scarlet Flow

- Added persistent Theme Engine and Settings > Themes.
- Added installable `.tthm` TPKG v2 theme packages with declarative-only safety policy.
- Added Scarlet Flow: pure black background, translucent scarlet Flow3D ribbons and red flow particles.
- Added shell style API v1 for bounded visual customization without changing shell behavior.
- Fixed Settings category entry so Themes and ★Debug Settings use the standard depth/slide/fade animation.
- Preserved all existing kernel/bootloader assets byte-for-byte.

## BuildFix19 — Orbis Library + global menu motion

- Replaced the Home Applications system hub with Orbis-style Library while preserving the tile icon/Title ID.
- Added Search, All, Folders, Games and Applications categories; no Purchased/store category.
- Library enumerates complete game/app/folder catalogs independently of Home's 15-tile limit, sorts A-Z and scrolls through overflow.
- Raised game catalog capacity to 256 while keeping folder membership capped at 128.
- Folder content selector now row-scrolls the entire candidate catalog and displays position with a scrollbar.
- Added parent-left/child-right hierarchical motion to folder child screens and Package Installer; Settings categories already share the same motion contract.

## BuildFix20 — Library fidelity / USB PUP updater / Package Installer session stability

- Added a dedicated Orbis-style Library glyph and forced Library to remain the final Home tile while preserving the legacy Applications glyph inside Library > Applications.
- Reworked Folder > Select Content to a 6x3 Orbis-style viewport with attached checkboxes, complete catalog scrolling and a vertical position indicator; parent/child screen motion remains animated.
- Expanded Package Installer discovery capacity to 96 entries, scans both `mass:` and `mass0:` package roots on every refresh, and continues to accept both `.pkg` and `.tthm`.
- Package installation now releases exclusive FileXio ownership immediately after each transaction so another package can be installed without returning to Home.
- Installed apps are inserted into the active application catalog incrementally instead of forcing a complete library scan.
- Added Settings > System > Atualizacao do software do sistema, using the Debug Settings glyph as requested.
- Added TPUP v1 `.pup` USB updates with build-number anti-downgrade checks, per-entry/table CRC32 validation, safe target allow-listing, staged extraction and rollback-safe multi-file publication.
- Successful PUP transactions discard temporary backups, so the updater does not create a persistent downgrade path.

## BuildFix22 — updater checksum hardening / Titan App compatibility messaging

- system updater now re-seeks the TPUP input stream before every chunk to avoid USB/file-position drift during multi-file extraction.
- updater now verifies each staged extracted file (size + CRC32) before publish, preventing false-success or false-corruption states.
- wLaunchELF native app now returns a dedicated incompatible-runtime init code when the running Titan kernel lacks the storage/launch ABI tail it requires.
- Titan App runtime now shows a clearer launch error: "Titan App requer um runtime mais novo (atualize o sistema)".
- PUP generation advanced to build 22.

## BuildFix21 — merged wLaunchELF native port / UI fidelity / first PUP test candidate

- Merged `apps/wlaunchelf` into the complete BuildFix20 Library/Updater baseline without dropping its PUP or Package Installer changes.
- Extended Titan App API v1 append-only with kernel-owned absolute storage listing/stat/read/write/mkdir/remove/rename and validated ELF handoff; existing v1 field offsets and package API version remain unchanged.
- Added the Titan-native wLaunchELF device/directory browser, cooperative 16 KiB file copy, deletion and `.elf` launch through Titan stage2.
- wLaunchELF remains a Titan-owned app: it does not call FileXio directly, load IRXs, own gsKit, call ExecPS2, or replace Titan input/render loops.


## BuildFix23 — hardware-safe TPUP single-owner transaction

- `TitanSystemUpdate_Begin()` no longer performs FileXio work before the storage worker is parked.
- Added PREPARING and VERIFYING phases; the PUP is authoritatively re-inspected and every payload entry is CRC-checked read-only before staging.
- PUP input and staging output are never open simultaneously on the USB device; extraction uses exact offset read-close / write-close chunks.
- Staged files are reopened and revalidated by size + CRC before atomic publication.
- Checksum failures now identify source-PUP, extraction-stream, or staging divergence.
- Added a tiny updater hardware self-test and regression coverage for forced 3-byte short transfers, deliberate payload corruption and rollback; BuildFix24 renumbers the self-test package to Build25 so anti-downgrade rules still accept it.
- Current system update generation advanced to BuildFix23.
## BuildFix24 — Flow multi-wave / system error popup / wLaunchELF visible-draw fix

- Theme Flow3D now exposes up to three theme-owned primary waves. Each wave declares independent vertical start/end coordinates, amplitude, alpha and phase without increasing the fixed GS/VU scratch allocation.
- Scarlet Flow 1.1.0 now uses two opposing non-horizontal primary waves instead of forcing a single ribbon through the vertical center of the screen.
- Added a dedicated System Error popup layer. `TITAN_NOTIFICATION_ERROR` is routed there and no longer enters the normal notification-card queue.
- Routine activity/success events no longer consume notification cards; cards remain for user-relevant info/warnings while the Home notification ticker keeps lightweight status visibility.
- Titan App read-only pointer validation now accepts the complete loaded module image (and retained module file backing) while writes remain constrained to writable sections/Titan-owned heap. This fixes the hardware case where wLaunchELF opened and remained responsive but its inner content was black because text/string draw commands were rejected.
- The Titan App renderer raises a dedicated system-error popup if an app submits rejected draw commands, preventing silent black-window failures.
- wLaunchELF package advanced to 0.1.1 and its host lifecycle test now requires visible panel/text draw commands.
- System update generation advanced to BuildFix24. The real BuildFix24 PUP carries `kernel.elf`, `WLAUNCHELF.pkg` and the updated Scarlet Flow configuration.
- The updater hardware self-test is moved to Build25 so it remains newer than the running BuildFix24.
- Complete host regression suite passes.


## BuildFix25 — forward-only TPUP hardware reader
- Replaced seek-based TPUP payload access with a forward-only sequential stream after BuildFix23 hardware telemetry proved CRC divergence during the read-only preflight pass.
- Enforces contiguous ordered payload entries, matching the TPUP builder contract.
- Verification and extraction each consume header/table sequentially and then stream payload bytes without `lseek`.
- Extraction alternates read/write RPC quanta and rereads staged files before publish.
- Keeps BuildFix24 Flow multi-wave, dedicated system-error popup, notification filtering and wLaunchELF render/runtime fixes.

## BuildFix27 — PS4-style Flow trajectories / direct wLaunchELF update PUP

- Hardware confirmation: BuildFix25 forward-only TPUP successfully installed the Build26 self-test package on a real PS2, so `.pup` is restored as the normal update path.
- Extended theme-owned primary waves with cubic vertical controls (`control1_y`, `control2_y`), slow per-wave vertical drift and independent thickness while retaining 1..3 primary waves and fixed GS/VU scratch ownership.
- Scarlet Flow advanced to 2.0.0 with two broad crossing cubic sheets instead of straight/near-horizontal center lines. The trajectory spans are theme data, not renderer constants.
- wLaunchELF advanced to 0.1.2 and adds a pointer-free Magenta draw canary so hardware can distinguish "module did not draw" from rejected text/string primitives; rejected drawing still raises the dedicated System Error popup.
- BuildFix27 real PUP updates both the kernel and the already-installed wLaunchELF directory directly (`tmod`, icon, manifest, notice), while also placing `WLAUNCHELF.pkg` in Package Installer for reinstall/recovery.
- BuildFix27 PUP carries Scarlet Flow 2.0.0 theme configuration in the same rollback-safe transaction.
- Full host regression suite passes, including forward-only TPUP short-I/O/rollback checks and the new Flow/wLaunch/PUP contract.

## RT1 BuildFix5 — native direct Internet assets
- Replaced the BF4 PC gateway runtime with direct PS2 HTTP/HTTPS asset fetching over PS2IP.
- Added BearSSL-based verified HTTPS transport with pinned build-time trust anchors.
- Added row-streamed PNG/JPEG conversion to cache-ready GS PSM16 TIMG assets.
- Kept selected/visible icon priority, background debounce, idle prefetch, bounded storage quanta and atomic cache publication.
- No bootloader changes and no runtime `assets.cfg` requirement.

## RT1 BuildFix8 — persistent diagnostic sessions
- Preserve `titan-current.bin` from the previous boot before the new session writes anything.
- Rotate five previous binary flight-recorder sessions.
- Add diagnostics disk header v2 with clean-shutdown marker.
- Add Debug Settings action to export the immediately previous boot to `mass:/TITAN/logs/titan-previous.txt`.
- BF7 v1 logs remain readable and are classified as `UNCLEAN_OR_RESET` because the old format carried no clean-exit flag.

## RT1 BuildFix9 — manual, network-decoupled asset downloads
- Wi-Fi/RJ45 reaching ONLINE no longer starts asset DNS/HTTP/TLS work automatically.
- Asset Internet worker is lazy and is only created by an explicit manual asset session or by enabling the optional automatic mode.
- Added Settings > Network > Dados de jogos with missing-only download, force update, asynchronous cache clear and automatic-download toggle.
- Automatic asset downloads are disabled by default on every boot for hardware validation.
- Manual sessions process all icons first and all backgrounds second, with one network transfer at a time and 8 KiB serialized storage writes.
- Added in-shell progress/background/cancel flow and BF9 Asset Flight Recorder events.
- Network Core and frozen BF6 bootloader are unchanged.

## RT1 BuildFix11 — downloaded-asset subsystem hardening
- Isolated the Asset Network Worker stack from service state, expanded it to 48 KiB, and added stack canaries/watermark diagnostics; BearSSL/HTTP scratch no longer lives on that worker stack.
- Hardened HTTP/TLS failure handling, redirect/URL parsing, body framing, request deadlines and TLS error recording while preserving direct PS2IP/BearSSL providers.
- Made PNG/JPEG decode bounded, malformed-input safe and cooperative: foreground/cancel can interrupt conversion and the worker yields every eight decoded rows.
- Made TIMG/cache transactions fail-closed with exact-size/CRC validation, immutable serial/startup identity, catalog/session generations, and recovery from read/write/staging failures.
- Fixed cancel/write buffer ownership, stale automatic-result contamination, manual retry/backoff bugs, stuck storage-stage/download-ready states, orphan cache cleanup and incorrect successful session-end reporting after asset failures.
- Added BF11 asset hardening contract plus malformed PNG/JPEG ASan/UBSan coverage.
- Network Core and the frozen BF6 bootloader remain byte-for-byte unchanged.

## RT1 BuildFix10 — manual asset pipeline hardening
- Kept Network Core connection completion completely decoupled from Asset Service DNS/TCP/TLS traffic; automatic downloads remain disabled by default.
- Hardened manual queue ordering to selected game -> remaining visible games -> rest of catalog, while retaining icons-first then backgrounds and one transfer at a time.
- Automatic mode now waits 30 seconds of continuous ONLINE state and requires an idle Home surface; user input makes automatic asset work pause/yield.
- Added HTTP phase tracing and named Flight Recorder boundaries for DNS, TCP, TLS, download, decode, cache and asset-session begin/end.
- Kept cache publication atomic with 8 KiB Storage Scheduler quanta and exposed the current game/kind in the progress UI.
- Network Core and the frozen BF6 bootloader remain byte-for-byte unchanged.
# 0.3.91-rc15 — TitanEYE/Ribbon BuildFix18 checkpoint

- Adds the pinned MIT MGB64 source without a ROM or extracted game assets.
- Extends TGR append-only to ABI v2 with textured triangles and explicit
  texture release, backed by a bounded 32-slot PSM16 game arena.
- Adds a four-page streaming ROM reader with GoldenEye size/title validation
  and z64/v64/n64 normalization.
- Scopes Project TitanEYE branding to the GoldenEye logo menu; the eye intro,
  timers, audio, routing and `title.c` remain unchanged.
- Replaces independent Flow-row waves with coherent Ribbon v2 frame transport
  while preserving all four existing palettes.
- Bumps the kernel to `0.3.91-rc15`, Titan UI to `0.9.35` and bootloader
  metadata to `0.1.18-rc15-usb-bdm-bf6`.
- Host regressions pass. No PS2DEV link, PCSX2 execution or hardware execution
  was performed; the five blockers in `ports/goldeneye/PORT_STATUS.md` prevent
  an honest executable TitanEYE TPKG at this checkpoint.
