# Project Titan: Magenta Edition — 1.0 RC1 Hardware Candidate

Date: 2026-08-22
Parent: user-supplied BuildFix30 target-fix tree that compiled and installed on real PS2
Status: **HOST-CLOSED / TARGET BUILD + PHYSICAL ACCEPTANCE REQUIRED**

This candidate consolidates the BF30 hardware feedback into one release line. It is intentionally **not branded as final 1.0 in frozen boot/kernel metadata yet**. The final version bump is gated on a clean PS2DEV rebuild and real-hardware acceptance.

## 1. Hardware regressions corrected

### 1.1 720p release path

The BF30 hardware log showed ~14.27 FPS, ~70 ms average frame time and ~80 ms attributed to render, while services/UI/storage were negligible. The active layout also reached only 0xC000 (49,152) bytes of VRAM headroom.

Root cause found in source: cold boot had regressed to the native/reference 1280x720 path instead of the already approved optimized composition path.

RC1 policy:

- physical output remains 1280x720p;
- release/default internal composition is **960x540 PSM16 -> 1280x720 linear upscale**;
- REFERENCE remains available for DEX A/B diagnostics and fail-safe comparison, but is not the cold-boot release default;
- corrupt/invalid preference fails safe to OPTIMIZED;
- composition -> sampling uses GIF ordering + TEXFLUSH and only one final GS FINISH/fence at presentation;
- optimized composition retains the 512 KiB reserved-headroom policy.

### 1.2 Flow/Ribbon visual regression

The built-in themes had drifted to the Ribbon reference profile with Snake disabled and VU1 deformation disabled. That produced a visually flatter result while moving work back toward EE/GS.

RC1 restores the hardware-proven Flow/Snake/VU1 release profile for Scarlet, Wine, Cyan and Twilight:

- Snake motion enabled;
- VU1 fine deformation enabled;
- Flow3D release geometry restored;
- reference_ribbon remains parser/runtime compatible for external themes, but is no longer the built-in release profile.

No visual simplification is used as the performance fix.

## 2. Welcome / controller / Bluetooth corrections

RC1 preserves and closes the input work discussed after the BF30 hardware test:

- first controller that actually interacts claims Player 1;
- temporary DS2 fallback identity is retired when a modern PADEMU identity takes over;
- Welcome accepts X from either the edge or current held state, preventing a lost confirmation during phase transition;
- PS/Home has an EE-side latch for the out-of-band PADEMU SIF event;
- Settings > Controllers is operational rather than diagnostics-only;
- startup backend can be persisted as Disabled / USB / Bluetooth;
- `Pair wireless controller` explicitly starts the PADEMU pairing/listening RPC;
- Sony classic-BT listening and Xbox BLE/HOGP scan paths are exposed by the same Titan UX;
- controller diagnostics remain available.

New controller/Welcome contract: **9/9 PASS**.

## 3. pikaVR — materially active and observable

pikaVR remains an internal optimization layer under the Titan/TGR texture ownership model. RC1 does not introduce a second VRAM manager.

Material producers include:

- texture fingerprint/deduplication;
- exact RGBA32 -> indexed PSMT8 + CLUT conversion with >256-color fallback;
- sampler-state cache;
- resident/eviction/accounting telemetry;
- per-frame renderer counters;
- measured VRAM savings.

RC1 additionally exposes pikaVR in:

- Settings > System;
- DEX Performance Overlay;
- Export Debug Report.

pikaVR -> Vlombis reporting is aggregated once per renderer frame. It does not instrument every draw call.

Host pikaVR harness: **PASS** (savings, sampler, lossless CLUT/fallback, VRAM budget, telemetry).

## 4. VlombisCore — all ten canonical domains materialized

VlombisCore keeps the evidence-driven rule: a capability bit is ACTIVE only after a real producer records work. Merely compiling code does not activate a domain.

Canonical domains and RC1 producers:

1. **STORAGE_QOS** — storage scheduler arbitration, foreground ownership and bounded quanta.
2. **BACKGROUND_QOS** — scan/audio/network/background scheduling and preemption.
3. **IO_COORDINATION** — exclusive ownership and cross-service storage arbitration.
4. **RENDER_STATE_CACHE** — aggregated pikaVR sampler/state-cache hits.
5. **ASYNC_PACKAGE_IO** — Package Installer/System Update bounded I/O quanta.
6. **LIBRARY_INCREMENTAL_SCAN** — incremental scanner steps, persistent cache hits, metadata reuse and Library view cache.
7. **AUDIO_BUFFER_QOS** — realtime/deferred audio storage refills.
8. **NETWORK_BACKGROUND_QOS** — steady-state network background polling with frame-pressure cadence control.
9. **FRAME_BUDGETING** — real frame duration from diagnostics; pressure feeds next-frame I/O/background policy.
10. **PIKAVR** — measured optimization/savings events from the renderer.

New full-integration contract: **16/16 PASS**.

### 4.1 Adaptive frame/storage policy

Default frame budget: 16,667 us.

Pressure model:

- pressure 2: frame > 1.5x budget;
- pressure 1: frame > budget;
- pressure clears after frame time drops below 75% of budget.

Package/System Update I/O remains single-owner and bounded. The normal quantum is 4 KiB; under pressure Vlombis can reduce it toward 1 KiB. Worker parking remains mandatory before main-thread FileXio package advancement.

Background network cadence can relax under frame pressure. Connection/bootstrap cadence is not delayed by this optimization.

## 5. Library/storage responsiveness

This is the largest storage-facing RC1 change.

### 5.1 Persistent catalog now avoids redundant ISO metadata reads

The pre-RC scanner could load a persistent catalog quickly, then reopen known ISOs during the verification scan to rediscover startup/version metadata.

RC1 snapshots the known-good game catalog before an incremental scan. If a known ISO path is still present and has valid startup metadata, that metadata is reused instead of reopening the ISO for the same information.

Telemetry exposes:

- `metadata_reused`;
- scan steps;
- estimated avoided bytes/work.

A force rebuild still bypasses reuse and performs the authoritative reconstruction.

### 5.2 Library sorted-view cache

The shell previously rebuilt, filtered and insertion-sorted the Library view repeatedly from count/input/draw paths.

RC1 introduces a generation-keyed Library view cache. The cached ordered view remains valid until one of these changes:

- Neutrino catalog generation;
- Applications catalog generation;
- folder count;
- category;
- search query.

Repeated navigation/draw requests copy the already sorted view instead of rebuilding O(n^2) ordering work. Cache-hit telemetry is aggregated before being sent to VlombisCore.

### 5.3 Catalog generations

Neutrino and Applications now expose monotonic catalog-generation counters. Mutations/refreshes invalidate the Library view deterministically without filesystem polling on the frame path.

Expected user-facing behavior: cached content should become usable immediately; discovery/verification continues cooperatively in background and should not make HOME/Library wait for a full rescan.

## 6. Observability

### Settings > System

Permanent entries (DEX not required):

- `pikaVR` — ACTIVE/OFFLINE, VRAM free, optimized path, internal resolution, savings, residency, cache hits/misses.
- `VlombisCore` — ACTIVE/READY, **N/10 active measured domains**, capability mask, pressure level and summarized work/savings.

### DEX Performance Overlay

Adds live PIKA and VLOM rows alongside FPS/frame timing. Vlombis reports active-domain count and pressure rather than pretending that all domains are active.

### Export Debug Report

Now records detailed pikaVR and Vlombis telemetry: VRAM/savings/cache/render, storage arbitration, package quanta/bytes, Library cache/reuse/scan, audio/network QoS, frame-budget hits/misses, pressure and avoided work.

## 7. Host validation campaign

The canonical host runner was promoted to include the four RC1 hardware-feedback gates.

Passed across segmented execution:

- core profile/VMC/storage fallbacks;
- Neutrino + UL launch paths;
- application discovery and Package Installer transactions;
- Unified Input, DualSense, Xbox USB/BT/BLE, Early Input, keyboard/mouse, overlays;
- fast boot catalog + **incremental metadata reuse**;
- theme runtime, XMB spline, Flow/Snake/VU1;
- release contract and source hardening;
- updater/PUP/rollback;
- renderer 480i/720p, optimized scaling, VRAM accounting/headroom/fault matrices;
- pikaVR ASan/UBSan host harness;
- VlombisCore ASan/UBSan host harness + scheduler evidence contract;
- wLaunchELF, Project NES/FCEUmm and app/network/VFS lifecycle contracts;
- Theme v4 lifecycle/degradation/profiler;
- BF29 input/API/store/repository/download/platform closure;
- Date/Time, Disc, localization;
- boot/TINS packaging;
- FMCB autonomous installer/ROM/layout/rollback/boot chain;
- BF30 target-source audit;
- HWFix2 720p/Flow gate;
- HWFix2 input/BT/Welcome 9/9;
- performance technology observability 8/8;
- full Vlombis integration 16/16.

### Source-clean audit exception

`test_buildfix29_open_source_readiness_contract.py` reports 80 payload/hash errors in the supplied BF30 tree because it contains already-built target/distribution artifacts and third-party sample material.

The **unmodified supplied BF30 baseline reports the same 80 ERROR records**. Therefore this is a pre-existing package-state/source-clean exception, not a regression introduced by RC1. It is not waived for a future public clean-source release; it is simply separated from the hardware-candidate functional gate.

## 8. Target build boundary

This execution environment does not provide:

- `mips64r5900el-ps2-elf-gcc`;
- `mips64r5900el-ps2-elf-ld`;
- the user's PS2DEV/PS2SDK target environment;
- PCSX2 or physical PS2 execution.

Therefore no newly linked RC1 `kernel.elf`, PADEMU IRX or installer ELF is claimed here. Existing target binaries inside the inherited tree are **stale BF30 artifacts** until rebuilt externally.

Canonical target build from this source candidate:

```sh
export PS2DEV=/opt/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
bash scripts/build_all.sh --verify-reproducible
```

Do not hardware-test the inherited `dist/` binaries as if they contained RC1; rebuild first.

## 9. Physical 1.0 acceptance gate

The version becomes final **1.0** only if the newly rebuilt target passes the following on real PS2:

1. Boot through the known-good Titan boot chain with no regression.
2. Confirm System reports `pikaVR ACTIVE` and Vlombis domains become ACTIVE as their producers are exercised.
3. Confirm 720p boots as **960x540 internal -> 1280x720 physical**, not Reference.
4. Confirm restored Scarlet/Wine/Cyan/Twilight Flow/Snake/VU1 appearance matches or exceeds the previously smooth Network + Flow baseline.
5. HOME/Settings/Library navigation is visually smooth with no recurring stalls.
6. Export diagnostics after at least 2 minutes of normal navigation and record FPS, avg/p95/worst frame, render_us, GS waits, VRAM free/peak/saved and Vlombis pressure.
7. Open Library with an already populated catalog; it must appear immediately while verification runs in background.
8. Reopen Library without storage changes; metadata reuse/cache counters must increase and navigation must not block on a full rescan.
9. Add/remove at least one game and confirm incremental discovery updates only the changed catalog state.
10. Exercise simultaneous audio + Library scan + network background work; audio must not starve and input/navigation must remain responsive.
11. Package install/update must remain transactionally correct while adaptive I/O is active.
12. Test DualShock 2, DualSense USB, Bluetooth pairing and Xbox path available to the tester; Welcome X/Player-1 behavior must be correct.
13. Test theme switching, dynamic theme, notifications, Package Installer, Disc flow, Neutrino/UL launch/return and PUP reboot.
14. Export `titan-debug.txt` plus persistent diagnostics for comparison with the BF30 14.27-FPS regression session.

Recommended release target is sustained 60-Hz-class navigation with no visible recurring stall. Final acceptance should be based on the captured hardware telemetry and visual behavior rather than a host-estimated FPS number.

## 10. Versioning decision

If the physical gate above passes, promote this exact source lineage to **Project Titan: Magenta Edition 1.0** and only then update release/version metadata as one controlled final-version commit. The frozen bootloader implementation remains frozen; only explicitly authorized metadata should move at final promotion.
