# Web (browser) build — bug/antipattern/performance backlog

**Date**: 2026-07-16 · **Audited at**: `f1a23aa` (+ in-flux working tree, see caveats)
**Method**: six parallel read-only audit lanes — controls/input, performance, WebGPU
rendering, audio, shell/storage/deploy, wasm32 correctness — each reporting only
code-cited findings; coordinator spot-verified every P0/P1 anchor against the
working tree. Cross-lane duplicates merged (several findings were independently
converged on by 2–3 lanes, which raises confidence).

**Severity**: P0 = breaks play / loses data / traps real users · P1 = major
playability degradation many users hit · P2 = latent bug or real waste worth
scheduling · P3 = polish/hygiene.

**Caveats**
- `src/platform/platform_sdl.c` and `src/platform/fast3d/gfx_webgpu.c` were being
  edited by another agent *during* this audit (rAF pacer, snapshot-rect bounding).
  Line numbers cite `f1a23aa` unless noted; re-anchor before fixing.
- WEB-005 must be coordinated with that in-flight work, not duplicated.
- Items marked **(H)** contain an unverified hypothesis; the item text says what
  measurement settles it.

---

## Status ledger (reconciled 2026-07-16 EOD — authoritative over per-item text)

**LANDED** (commit · items):
- `ae1084f` wave-1 — WEB-001, 003, 004, 009, 010, 033, 040
- `f17d6a9` wave-2 (adopted) — WEB-002, 006, 007, 008(js), 011, 012, 013, 014, 015
- `3c3ffc0` batch A — WEB-008(page), 019, 029, 030, 031, 032(shell), 035, 039,
  062(shell), 063, 064, 065, 066
- `b27a538` batch DE — WEB-017, 041, 042, 044, 045, 046, 047, 048, 059
- `7c2d742` batch B — WEB-032(build), 034, 057, 060, 061
- `fc47a7f` batch C — WEB-020, 025, 026(+limits retry), 028(diagnostic half),
  038, 049, 051, 053, 055, 058
- final-review follow-ups (same day) — beforeunload/recovery interaction,
  pointer-lock confirm-latch relock reset, WEB-053 ordering-invariant comment.

**CLOSED WITHOUT CODE**: WEB-005 (pacer landed pre-program in b1525bf/73eb35e;
residual = deploy); WEB-024 (bind-group cache proven ABA-safe by review);
WEB-067 (owner-reported dead browser wheel = stale served build; path proven
correct end-to-end — rebuild + hard-reload).

**DEFERRED-ROUND LANDED 2026-07-16 late** (`5887388..b68a974`, all task-reviewed
+ composition-reviewed READY, tape 7/7 hashes == pre-round baseline):
- `5887388` WEB-016 (per-axis unscale + fractional carry — review caught the
  width-only Y-skew) + WEB-018-lite (shell sensitivity slider + invert-Y via
  --config-override, localStorage-persisted)
- `75c8741` WEB-043 (drift-gate ctest `web_controls_doc_drift` w/ F-key
  regression lock incl. the web H-help arm; self-test 7 seeded drifts red)
- `fd53bf3` WEB-056 (libm de-shadow: acosf/asinf/atan2f → ge007_* on wasm;
  sinf/cosf proven never-linked — dormant guard; nm coverage proof)
- `bdb5a3e` WEB-027 (animated noise, GL-identical constants) + WEB-050
  (shader-slot eviction w/ release) + WEB-052 (modern-mesh dynamic-offset UBO
  ring) + WEB-023-lite (viewport/scissor dedup)
- `460d991` WEB-037 (gfx_ptr per-stage clear — kills stale-entry shadowing AND
  tombstone accumulation; max-probe hoist; routes 6/6 quiet, tape byte-exact)
- `b68a974` composition follow-ups (lroundf decl, noise comment)

**RELEASE CLOSE-OUT 2026-07-17** (`5d06bdd..b845955`): WEB-036 ruled (separate
hash lineage, WEB.md); `b621f74` boot-smoke harness (ctest `web_boot_smoke`);
`d820ff1` WEB-068 menu-glyph P0 (bind-group cache handle-reuse ABA — the
WEB-024 finding vindicated; fixed with view/BGL-release invalidation, 10/10
deterministic-clean repro) + WEB-023 residual (one vertex writeBuffer/frame,
~20% native Dam CPU); `b845955` faithful HUD web defaults (reticle
only-while-aiming, no minimap — HUD half of --faithful). Owner confirmed the
glyph fix and the faithful HUD in-browser.

**DEFERRED POST-RELEASE — Dam parity program** (hunt report:
docs/audit/DAM_PARITY_HUNT_2026-07-17.md):
- **DAM-R1** (P1, confirmed, WebGPU-ONLY — GL-clean A/B): over-bright
  sky/backdrop seam at portal boundaries (intro flyby + walkway); backend
  blend/fog mechanism untraced. THE top post-release fix.
- **DAM-R2** (P2, suspected, same-wrong both backends): establishing-shot
  distant shore missing — T25/T13b admission residual; stock verdict pending.
- Un-swept: outro/bungee cinematic, interior rooms ~30-60, stock ares capture.
- Negative result: literal water-through-geometry NOT reproducible — reservoir
  water occludes correctly everywhere inspected on both backends.

**PERF PROGRAM LANDED 2026-07-17/18** (execution ledger:
`docs/audit/PERF_BACKLOG_2026-07-17.md` — waves 1-3 + review; web-affecting
items reconciled here):
- **WEB-039 superseded** → PERF-032 `ba69d94`: streaming `instantiateWasm` +
  fetch-before-gate (the MIME assumption that justified non-streaming was
  disproven); WEB-032/033 guards preserved.
- **WEB-062 build half** → PERF-033 `0091980`: INITIAL_MEMORY 256→128 MiB +
  MAXIMUM_MEMORY 512 MiB.
- **WEB-013 further mitigated** → PERF-035 `d0bad20`: level-load cooperative
  yields (no frozen tab) + pre-teardown audio prefill to the 12-frame cap.
  - **WEB-069 audio latency (FID-0141):** the audio occupancy controller in
    `audi_port.c` `portAudioSizeFrame` used **30 Hz frame units in a 60 Hz pump**,
    two ways: (1) the drain floor `align16(g_FrameSize/2)` = 368 sat *above* true
    60 Hz consumption (367.5), so once the PERF-035 prefill filled the queue to the
    12-frame (~400 ms) web cap the controller could never drain it — measured
    ~240–400 ms fire-to-hear; and (2) the controller base was a full 30 Hz frame
    (736 = 2× realtime), which parks a proportional controller's steady state one
    full frame *above* its target (83 ms vs a 50 ms target) even once draining.
    Fix: floor → 352 (one quantum below realtime, restores drain authority) and
    base → 368 (realtime, so the fixed point is the target). Measured native queue
    134 → 58 ms; web mean ~300 → ~93 ms; zero underruns; deterministic/tape
    byte-identical.
  - **WEB-069 parity fix — AudioWorklet (`web_audio_worklet.c`):** the remaining
    ~55 ms web-vs-native gap was architectural — the `SDL_QueueAudio` pump *and*
    the ScriptProcessorNode both ran on the main thread, so stalls spiked the
    queue and the 2048-sample SPN buffer (42.7 ms) was un-shrinkable glitch
    insurance. Fixed by a plain-JS AudioWorklet + ring buffer that drains on the
    audio render thread: **no SharedArrayBuffer / COOP-COEP (works on plain
    Pages), no WebGPU-build risk**, web-only with the SPN kept as fallback.
    Measured device-side latency ~136 ms → ~58 ms (native parity), audio flowing,
    zero idle-steady-state underruns. The full wasm-in-worklet (shared-memory)
    option was rejected — needs headers Pages can't serve, risks the
    ASYNCIFY+growth WebGPU build, and buys nothing over the ring while the synth
    stays sim-coupled. True sub-10 ms parity would need decoupling the synth from
    the 60 Hz sim tick (a large fidelity risk); not warranted.
  - **FID-0141 low-FPS residual closed:** the live controller's 368-sample base
    was correct only while the render/audio pump sustained 60 Hz; at 40/30 fps
    its fixed point fell to roughly 33/17 ms, stripping the jitter cushion on
    weak hardware. A measured 1/4-rate pump-interval EMA now raises only the
    production base (bounded by the existing synth maximum), while 60 Hz stays
    exactly 368 and deterministic replay never samples time. Real 30 fps native
    A/B: mean pre-enqueue occupancy 16.27 → 49.35 ms, zero drops/underruns on
    both sides. Durable model + trace evidence:
    `docs/fidelity/derivations/FID-0141-audio-pump-ema.md`.
- **WEB-012 near-term half** → PERF-010 `e79d871`+`97180bd`:
  `Audio.QueueTargetFrames` LIVE knob (platform-aware max). Worklet half still
  deferred (→ PERF-039).
- **WEB-054 async half** → PERF-005 `4db633e` + **PERF-005b `1166fc1`** (the
  async draw-drop presented incomplete frames — sky-flood bleed; fixed by
  present-hold, root-caused via CDP repro). Prewarm half still open (→ W4.3).
- **WEB-021 residual measured & deferred (data)** → PERF-007: 0/3,276
  memory-blend splits empty on Facility/Runway/Dam; coalescing is a TBDR win
  unmeasurable on the CPU-bound M3 Max.
- Also web-facing: PERF-030 `639cffc` (web RenderScale native-res default —
  the single biggest web pixel-load lever), PERF-014 `4765099` (per-draw
  SetPipeline/SetBindGroup dedup), PERF-008 `6834759` (direct-to-surface
  present, no trailing copy), plus the universal gfx_pc.c wins (PERF-002/003/
  004/006/012/013/015/016 — the material split alone is ~12-15% CPU).
- **W4.1 `0abbd19`**: browser regression lane (`web_frame_probe` ctest —
  frame-completeness under CPU throttle + movement; the net WEB-022 wanted).

**WAVE-4 CLOSE 2026-07-18**: **WEB-022 LANDED** `0933304` (PERF-031: Asyncify
narrowed via IGNORE_INDIRECT + explicit ADD spine — wasm 4.02→3.12 MB, −22.4%);
**WEB-054 prewarm half LANDED** `0398729` (record/replay manifest, full scope —
shaders buildable at load); PERF-036 SW `ad1350b` (offline + instant repeat
visits; dist is SIX files now); browser lane `web_frame_probe` standing.

**STILL DEFERRED / OPEN**:
WEB-018 full (in-game settings overlay on web — seam unblocked via WEB-055),
WEB-023 residual (per-frame writeBuffer batching),
WEB-028 fix-half (attr packing — diagnostic in place, no combiner observed
tripping 16), WEB-036 (ffp-contract — OWNER: requires native baseline
re-record), WEB-050b (eviction → gfx_pc invalidation hook, only if
WGPU_SHADER_MAX is ever lowered), WEB-012 worklet half (→ PERF-039, gated on
COOP/COEP via PERF-036's coi-serviceworker path — the SW now ships, so the
path is available). Deploy to Pages = owner-gated (`web-demo.yml`
workflow_dispatch).

**Verification at HEAD**: native build clean; renderer_parity + both apertures +
sim_state_hash + campaign_route + input/config ctests green; tape gate 7/7
byte-exact (quiet machine, exit-0); web Release build clean (5-file dist, wasm
~3.99 MB, magic-scan clean, version stamp live); Debug SAFE_HEAP genex proven
under emcmake. Final whole-branch review: READY-WITH-FOLLOWUPS → follow-ups
landed same day. Owner checklist: browser smoke (mute ramp by ear, pointer-lock
feel, device-lost panel, DPR>1 sharpness, two-tab lock refusal, wheel after
rebuild), then redeploy.

---

## P0 — trap/blocker class

### WEB-001 [P0] Debug hotkeys are live on web and the page tells users to press them
- **Lanes**: controls + shell + rendering (3× independent)
- **Where**: `platform_sdl.c:3287` (F1 → `g_pcDebugFlyCamera`), `:3291` (F2 →
  screenshot), `:3319-3337` (F3–F12 → unconditional `bossSetLoadedStage` level
  warp, F10=Surface2, F11=Jungle); `stubs.c:6256` (fly-cam zeroes ALL pads);
  `CMakeLists.txt:1955` (`MGB64_APP OFF` → no ui_overlay.cpp on web, so nothing
  swallows F1/F10); `web/index.html:79,113-116,190-192` + the H help text
  (`platform_sdl.c:3294-3312`) advertise F1=menu, F10=FPS, F11=fullscreen.
- **Symptom**: user follows on-page instructions → F1 silently kills all gameplay
  input (fly camera, looks frozen/haunted); F10 warps mid-mission to Surface 2,
  destroying the session; F11 warps to Jungle *and* SDL preventDefaults it so
  browser fullscreen never fires. Gamepad "Back/View = settings" row is a dead
  button (overlay absent, Back reserved away from gameplay in
  `input_bindings.c:237`).
- **Fix**: gate the F1 fly-cam and F3–F12 warp block behind a dev flag
  (`GE007_DEV_HOTKEYS` env, or `#ifndef __EMSCRIPTEN__`); rewrite index.html
  System/Gamepad rows + `#overlay-hint` + H text to match the web binary
  (Esc/Tab watch, M mute, H help; engine FPS overlay is on by default). Optional
  follow-up: actually ship the overlay (see WEB-018).
- **Effort**: S (gate + docs) / M (ship overlay)

### WEB-002 [P0] Wrong-content 12 MB ROM = invisible rejection, permanent black screen, re-poisoned every visit
- **Lanes**: shell
- **Where**: `web/mgb64-shell.js:100-120` (no `onExit`/`printErr`/`onAbort`
  passed; `callMain` return ignored), `:39-47` (`loadStoredRom` checks size
  only); `rom_io.c:108-145` (engine refuses → stderr + `return 1`);
  `rom_offsets.c` (US-only offsets patched onto any `GOLDENEYE`-titled ROM, so
  PAL/J passes the title scan then breaks).
- **Symptom**: any 12 MB non-working file (other game, corrupt dump, PAL/J) boots
  to a black canvas with diagnostics only in devtools — and the bad file is
  already in OPFS, so every future visit shows "ROM ready" and repeats the
  failure. User must guess that "Forget stored ROM" fixes it.
- **Fix**: pass `printErr`, `onExit`, `onAbort` to `createMGB64`; nonzero exit →
  re-show gate with the last stderr lines + a "Forget stored ROM" shortcut.
  Bonus: `crypto.subtle.digest("SHA-1")` against `ge007.u.sha1` at pick time for
  instant "wrong game/region" feedback (content check, not just size).
- **Effort**: M

### WEB-003 [P0] Capability gate passes on `"gpu" in navigator` alone — adapter-less browsers boot to inert black canvas
- **Lanes**: shell + rendering (2× independent)
- **Where**: `web/mgb64-shell.js:71-77`; `gfx_webgpu.c:380-383,452-455`
  (bring-up failure → `backend stays inert`, stderr only).
- **Symptom**: `navigator.gpu` exists but `requestAdapter()` resolves null on
  Chrome/Linux default configs, blocklisted GPUs, some Android. Those users pass
  the gate, store a ROM, hit Play → sim/audio may run against a permanently
  black canvas.
- **Fix**: make `gate()` async and require `await navigator.gpu.requestAdapter()`
  non-null; plus export a JS hook the C bring-up failure path calls to swap the
  canvas for a human-readable error panel (also serves WEB-025).
- **Effort**: S

---

## P1 — major playability

### WEB-004 [P1] F2 screenshot freezes the tab for minutes: readback wait pumps with NULL instance
- **Lanes**: wasm + controls
- **Where**: `gfx_webgpu.c:609` and `:2157` — `WGPU_COMPAT_WAIT(mr.done, NULL,
  s_device, 100000)`; browser pump only calls `wgpuInstanceProcessEvents` when
  `instance` non-NULL (`gfx_webgpu_compat.h:28-32`, its own comment says the
  callback *only* fires during ProcessEvents).
- **Symptom**: on web `mr.done` never becomes true → 100 000 × `emscripten_sleep(1)`
  (~4 ms setTimeout clamp) ≈ several minutes of frozen tab, then the readback
  still fails. F2 is a live key (WEB-001). Native unaffected (pump prefers device).
- **Fix**: pass `s_instance` at both call sites. Independently: disable F2 on web
  or route the BMP through a JS download blob (it currently writes to MEMFS
  where the user can never reach it, ~900 KB heap leak per press).
- **Effort**: S

### WEB-005 [P1] Land the rAF frame pacer — deployed build hitches (40–45 fps 1% lows); keep the unconditional-yield form
- **Lanes**: performance
- **Where**: `platform_sdl.c:3670-3697` (in-flux; HEAD f1a23aa still ships the
  `emscripten_sleep(rem_ms)` pacer whose 1–4 ms setTimeout clamp overshoots the
  16.667 ms deadline).
- **Symptom**: periodic 20–25 ms frames for every current player. CRITICAL
  invariant for the fix: the rAF wait is the engine's **only** recurring
  Asyncify yield point — an intermediate working-tree version that skipped the
  yield when behind deadline would *never yield* on sustained sub-60fps machines
  (no present, no input, no timers → "page unresponsive"). The do-while
  (always ≥1 rAF per frame) form degrades gracefully instead.
- **Fix**: coordinate with the in-flight branch; ensure the committed version is
  the do-while form with a comment stating the invariant ("never gate the sole
  yield point on being ahead of deadline"); rebuild + redeploy dist (the shipped
  wasm predates even HEAD).
- **Status**: DONE — the do-while unconditional-yield pacer landed in `b1525bf`,
  and the `73eb35e` hidden-tab fallback covers the background-tab (no-rAF) case.
  Residual is deploy hygiene (rebuild + redeploy the shipped wasm), not code.
- **Effort**: S (coordination + deploy)

### WEB-006 [P1] Web build silently ships the STUB sound player — `PORT_SOUNDPLAYER_REAL` never reaches `ge007_web`
- **Lanes**: wasm (verified: `CMakeLists.txt:1766-1767` defines it for `ge007`,
  `:2306-2307` for `ge007_lib`, nothing for `ge007_web`; `snd.h:12-14` therefore
  selects `PORT_SND_STUBS` since the directory-wide `PORT_FIXME_STUBS` applies).
- **Symptom**: every SFX in the browser uses the legacy stub player's
  voice-allocation/pan/priority/release semantics instead of the real
  ALSndPlayer path (`snd.c:1184-3531`) — unlabeled native/web engine divergence
  in a "same engine" product; defeats future audio-parity checks.
- **Fix**: `target_compile_definitions(ge007_web PRIVATE PORT_SOUNDPLAYER_REAL)`
  inside the existing `if(PORT_SOUNDPLAYER_REAL)` block; rebuild, listen-test.
- **Effort**: S

### WEB-007 [P1] Esc pointer-lock desync: watch needs two presses; camera tracks the *unlocked* cursor
- **Lanes**: controls
- **Where**: `platform_sdl.c:3268-3281` (Esc handler assumes it sees the
  keydown), `:3410-3423` (motion accumulated whenever `g_mouseGrabbed`).
- **Symptom**: while pointer-locked the browser consumes Esc as the lock-exit
  gesture and delivers **no keydown**; `g_mouseGrabbed` stays 1 → the watch
  doesn't open (page says it will) and the in-game camera keeps turning with the
  visible cursor ("possessed camera") until a second Esc.
- **Fix**: reconcile `g_mouseGrabbed` with `document.pointerLockElement` each
  frame (or on pointerlockchange) under `__EMSCRIPTEN__`; treat detected lock
  loss during gameplay as the Esc press (set `g_pcEscapePressed = 1`, stop
  motion) — the standard browser-FPS pattern.
- **Effort**: S/M

### WEB-008 [P1] Tab-loss chords: Ctrl+W closes the tab (Ctrl = crouch), mouse back/forward navigate away — no unload guard
- **Lanes**: controls
- **Where**: `platform_sdl.c:3282-3286` (LCtrl crouch); SDL emscripten port
  explicitly opts out of the beforeunload confirm and doesn't consume mouse
  buttons 3/4; `web/mgb64-shell.js` registers no `beforeunload`.
- **Symptom**: crouch-forward (Ctrl+W) — the most common FPS chord — instantly
  closes the tab (unpreventable by preventDefault; ditto Cmd+W); thumb-button
  presses history-navigate. Mission progress gone without warning.
- **Fix**: shell `beforeunload` prompt once booted (covers both);
  `navigator.keyboard.lock(["ControlLeft","KeyW",...])` when fullscreen;
  de-emphasize Ctrl in the controls table.
- **Effort**: S

### WEB-009 [P1] Boot failure wedges the page: `booted` stays true, Play stays disabled, canvas stays up
- **Lanes**: shell
- **Where**: `web/mgb64-shell.js:98-103` (`booted = true` before any await;
  gate hidden, canvas shown), `:147-150` (catch never resets state).
- **Symptom**: any transient failure (network blip on the wasm fetch, syncfs
  rejection) → "Boot failed" with a permanently dead Play button; only a manual
  reload recovers. Also close the leaked live AudioContext in this path (audio
  lane: SPN keeps burning CPU after failed boot).
- **Fix**: in the catch: `booted = false; canvas.hidden = true;
  $("play").disabled = false;` + `Module.SDL2?.audioContext?.close()`.
- **Effort**: S

### WEB-010 [P1] No crash surface: a wasm trap mid-game is a silently frozen canvas
- **Lanes**: shell + wasm (2×)
- **Where**: `main_pc.c:543-546` (by design, "the browser tab is the crash
  boundary" — but the host provides nothing); shell passes no `onAbort`;
  `boot().catch` only covers boot (Asyncify: `callMain` returns at first unwind,
  later traps bypass it).
- **Symptom**: any mid-mission RuntimeError (the 68afbc6 crash class existed in
  the field) = frozen last frame, no message, user can't distinguish crash from
  hang; doesn't know a reload preserves their auto-save.
- **Fix**: pass `onAbort`; add `window.onerror`/`unhandledrejection` handlers
  that overlay "The game crashed — reload to continue from your last auto-save"
  and fire one last `FS.syncfs(false, …)`.
- **Effort**: S

### WEB-011 [P1] iOS/iPadOS Safari 26 passes the gate but the game is unplayable — no warning before the user invests
- **Lanes**: controls + shell (2×)
- **Where**: `web/mgb64-shell.js:71-77`; engine input is keyboard/mouse/SDL
  controller only (`stubs.c:6352-6424`); no pointer lock on iOS; touch = fire-only.
- **Symptom**: phone user is told their browser qualifies, uploads 12 MB, boots —
  and can only fire. Top future confusion report.
- **Fix**: detect `matchMedia("(pointer: coarse)")` + `navigator.maxTouchPoints`
  in `gate()`; show "needs keyboard & mouse (or gamepad)" — still allow boot
  (paired keyboards/controllers exist). Real touch controls = separate future epic.
- **Effort**: S

### WEB-012 [P1] Audio: 512-sample main-thread ScriptProcessorNode — ~10.7 ms deadline vs frames that routinely exceed it
- **Lanes**: audio + performance (2×)
- **Where**: `audio_pc.c:28` (`PORT_AUDIO_SAMPLES 512`), `:1002-1006` (no web
  override); SDL 2.32 emscripten driver = deprecated main-thread SPN at the
  context rate (48 kHz → 512 frames = 10.67 ms). **(H)** magnitude — settle via
  `under=` counter (`audi_port.c:679`) + A/B 512 vs 2048.
- **Symptom**: any sim+encode frame longer than ~one SPN period (routine on wasm;
  guaranteed in heavy scenes, first-sight pipeline compiles, GC) → crackle.
  Native tuned this for a real-time CoreAudio thread; the web has no such thread.
- **Fix**: default `Audio.DeviceSamples` to 2048 under `__EMSCRIPTEN__` (~+32 ms
  latency, A/B first); long term AudioWorklet/SDL3 sink. Also clamp the setting
  to pow2 ∈ [256,2048] before `SDL_OpenAudioDevice` — out-of-range persisted
  values make `createScriptProcessor` throw and permanently brick boot
  (`IndexSizeError` → "Boot failed" every visit until site data cleared).
- **Effort**: S

### WEB-013 [P1] Every level load exceeds the 167 ms audio queue cap → guaranteed hard audio gap
- **Lanes**: audio
- **Where**: `stubs.c:428` (`AI_QUEUE_LIMIT_FRAMES 5` = 166.9 ms max buffered);
  pump only runs once per frame (`platform_sdl.c:3767-3773`); loads/music
  decompress/`WGPU_COMPAT_WAIT` spins produce nothing meanwhile.
- **Symptom**: music/SFX gap (preceded by crackle) on every stage transition and
  any long synchronous stretch.
- **Fix**: on `__EMSCRIPTEN__` raise the cap (10–12 frames) and let the existing
  occupancy controller (`audi_port.c:465-475`) pre-fill deeper before known-long
  operations — only the cap blocks it today.
- **Effort**: S (cap) / M (load-aware pre-fill)

### WEB-014 [P1] Minimap scissor rect silently discarded on WebGPU — minimap is default-ON
- **Lanes**: rendering (verified: `gfx_webgpu.c:2068-2070` voids all five
  scissor params; GL/Metal both apply it; `g_pcMinimapEnabled = 1` default).
- **Symptom**: minimap geometry the layout clips (lines/blips while moving or
  rotating) can draw outside the minimap window over the game view — on the
  default backend everywhere, not just web.
- **Fix**: Y-flip the rect (`sy = fb_h - (y+h)` as Metal does), clamp via
  `wgpu_clamp_rect`, `wgpuRenderPassEncoderSetScissorRect` before the draw.
- **Effort**: S

### WEB-015 [P1] RenderScale blockiness root-caused: device requested with DEFAULT limits + hardcoded 8192
- **Lanes**: rendering (upgrades the WEB.md "known item" from mystery to fix-spec)
- **Where**: `gfx_webgpu.c:398-401` (no `requiredLimits` in device descriptor),
  `:2404-2406` (`return 8192` hardcoded), consumed by `gfx_pc.c:4671-4709`.
- **Symptom**: the observed 8192×6144 clamp is the **WebGPU default limit**, not
  the hardware (most desktop GPUs support 16384). Large viewports at the default
  RenderScale 2 hit the clamp → resample blur/blocking.
- **Fix**: `wgpuAdapterGetLimits` → request `maxTextureDimension2D` up to the
  adapter's max in `requiredLimits` → return the *granted* value from
  `gfx_webgpu_max_offscreen_dim()` (8192 fallback). Also raise the matching
  hardcoded reject in `wgpu_upload_texture` (`gfx_webgpu.c:1449-1450`).
- **Effort**: S

---

## P2 — schedule soon

### Input feel
- **WEB-016 [P2] Mouse sensitivity varies with browser window size.** SDL
  emscripten rescales `movementX` by (SDL logical width ÷ canvas CSS width);
  window is created at `Video.WindowWidth`=1440 while CSS pins the canvas to the
  viewport → ~1.6× feel difference between a 900 px window and fullscreen, and
  never matches native at the same sensitivity. Fix: under `__EMSCRIPTEN__` undo
  the scale (× `css_width / window_w`) or keep the SDL window size synced to CSS
  size so the ratio is 1. (`platform_sdl.c:514,2995-3000`; consumed
  `lvl.c:5929,6003-6008`.) Effort M.
- **WEB-017 [P2] Recapture-click fires the weapon.** `g_pcMouseRegrabFrame`
  suppresses exactly one poll (`platform_sdl.c:3343,3360`; `stubs.c:6419-6426`)
  but a human click spans 4–9 polls → every pointer-lock recapture discharges
  the equipped weapon (alerts guards, wastes ammo). Fix: suppress until all
  mouse buttons released (mirror `s_overlayReleaseLatch`). Effort S.
- **WEB-018 [P2] No way to change any input setting on web** (sensitivity,
  invert-Y, bindings, deadzone) — no overlay (`MGB64_APP OFF`), no editable ini,
  shell passes only 3 args. Fix short-term: shell-side sensitivity slider +
  invert-Y checkbox appending `--config-override Input.*=…` to `callMain`;
  long-term: compile the overlay for web (unblocks WEB-001's F1 promise; note
  `gfx_webgpu_imgui.cpp` must first route through the compat seam — WEB-059).
  Effort S (shell) / M (overlay).
- **WEB-019 [P2] Alt is hardcoded L_TRIG and Alt-combos aren't preventDefaulted**
  (`stubs.c:6402`; SDL nav-key list checks ctrlKey only) → Alt+D focuses the
  address bar, bare Alt opens Firefox's menu, focus loss drops all keys
  mid-firefight. Fix: shell-side capture-phase `keydown` preventDefault for
  `altKey` combos while the game runs; document Alt's role. Effort S.

### Rendering & GPU robustness
- **WEB-020 [P2] No devicePixelRatio handling** — resolution base is CSS pixels
  (`gfx_pc.c:4611-4643`), so on DPR-2 displays the default "RenderScale 2 SSAA"
  is actually 0× supersampling, and RenderScale 1 renders at half device
  resolution (blurry upscale). Fix: multiply CSS size by
  `emscripten_get_device_pixel_ratio()` before RenderScale so the knob means the
  same thing as desktop; the max-dim clamp (WEB-015) keeps it safe. Effort S.
- **WEB-021 [P2] Memory-blend (fence/glass) splits the scene render pass per
  qualifying batch** (`gfx_webgpu.c:1692-1725` end pass → full-scene
  CopyTextureToTexture → new pass with Load/Load). No readbacks (verified,
  good), but on tile-based GPUs (Apple Silicon = every macOS browser, all
  mobile) each split = full color+depth store+reload (~tens of MB traffic per
  split at RenderScale 2). **(H)** magnitude — settle with a pass-count +
  frame-time measure in Facility/Runway on an Apple-GPU browser. Fix: the
  in-flight rect-bounded copy helps the copy half (don't redo it); additionally
  skip the split when the batch rect is empty, and coalesce consecutive
  non-overlapping memory-blend batches into one snapshot. Effort M.
  **Status**: rect-bounded copy is DONE (landed; don't redo). Remaining =
  empty-rect skip + batch coalescing.
- **WEB-022 [P2] Blanket `-sASYNCIFY` instruments essentially the whole engine**
  for three real unwind sites (`CMakeLists.txt:1987`; no
  ASYNCIFY_ONLY/ADD/IGNORE_INDIRECT anywhere) — typical 1.5–3× tax on
  instrumented code paths, every frame, plus binary size. Fix: experiment with
  `-sASYNCIFY_IGNORE_INDIRECT` + explicit ADD list for the
  main→…→`platformFrameSync` chain and gfx wait helpers; validate with the tape
  gate. Optional second build flavor with `-sJSPI` (Chrome) later. Effort M.
- **WEB-023 [P2] Draw-path churn: per-batch `wgpuQueueWriteBuffer`, single-entry
  bind-group cache, unconditional per-draw viewport/scissor**
  (`gfx_webgpu.c:1814,1840-1872,1883-1899`) — ~1000+ wasm↔JS crossings/frame,
  bind-group create/release nearly every draw when materials alternate. **(H)**
  exact ms — profile one frame in Chrome. Fix: one writeBuffer per frame from a
  shadow buffer; small keyed bind-group cache (64–256 entries); skip redundant
  viewport/scissor sets. Effort M.
  **Status**: single-entry bind-group cache half is DONE — `b1525bf` replaced it
  with a 512-entry cache. Remaining = per-batch `wgpuQueueWriteBuffer` coalescing
  (one shadow-buffer write/frame) + skipping redundant per-draw viewport/scissor.
- **WEB-024 [P2] Bind-group cache keyed on raw handle values — stale-hit hazard
  after handle reuse (emdawnwebgpu IDs are recycled)** (`gfx_webgpu.c:1075-1076,
  1836-1870`). **(H)** — settle by checking emdawnwebgpu's ID reuse policy or
  logging collisions. Symptom would be sporadic wrong textures after level
  transitions, web only. Fix (also serves WEB-023): key on engine texture id +
  upload generation, or invalidate the cache in delete/upload. Effort S.
  **Status**: SETTLED-SAFE by the `b1525bf` review — the cached bind groups hold
  strong references to their textures, so a handle value can't be recycled under
  a live cache entry (no ABA/stale-hit). No fix required; revisit only if the
  cache is ever changed to hold weak refs.
- **WEB-025 [P2] No device-lost handler** (`gfx_webgpu.c:398-401` sets only
  uncapturedError) — GPU process restart/driver reset = permanently frozen
  canvas, sim keeps running. Fix: register `deviceLostCallbackInfo` → flip
  `s_ready=false` + surface the WEB-003/WEB-010 error panel ("graphics device
  lost — reload"). Full re-init later. Effort S (message) / M (re-init).
- **WEB-026 [P2] Async bring-up: timed-out callbacks write into dead stack
  frames; web wait budget only ~4 s** (`gfx_webgpu.c:370-406` stack-allocated
  `areq`/`dreq` + `WGPU_COMPAT_WAIT(…, 1000)`; slow cold-start adapter
  enumeration can exceed it → silent wasm stack corruption when the promise
  finally resolves, on exactly the machines where bring-up is slow). Fix: make
  request structs static (or heap + leak on timeout); raise the two boot wait
  budgets on web; release instance/surface/adapter on failure paths (folds the
  P3 leak). Effort S.
- **WEB-027 [P2] SHADER_NOISE is a frozen position hash on WebGPU** — GL feeds
  `frame_count` per frame (`gfx_opengl.c:629-632`), WGSL has no frame input
  (`gfx_webgpu_shader.c:291-301`) → N64 static/fizz effects render as a fixed
  pattern on the default backend. Fix: small per-frame uniform or fold a frame
  counter into the hash without a new bind slot. Effort M.
- **WEB-028 [P2] Maximal combiners can exceed WebGPU's 16 vertex-attribute /
  inter-stage limits** (`gfx_webgpu_shader.c:165-240` can emit up to 23 attrs;
  `vattrs[24]`) → pipeline creation fails → batch silently skipped (missing
  geometry) with console-only validation error. **(H)** whether a real GE
  combiner crosses 16 — add a loud log at `num_attrs > 16` to find out. Fix:
  pack scalar clamp/mask limits into shared vec4 attributes. Effort M.

### Storage, sessions, ops
- **WEB-029 [P2] `FS.syncfs` errors swallowed + unguarded overlap + no dirty
  check** (`mgb64-shell.js:114-116`: `() => {}` on both the 5 s interval and
  pagehide; emscripten syncfs is not reentrant-safe; every tick serializes /save
  even when unchanged). Symptom: silent permanent save-loss while the page
  promises auto-save. Fix: in-flight flag to skip overlap; on error show a
  persistent "couldn't save to browser storage" banner + retry with backoff;
  dirty-flag to skip idle ticks. Effort S.
- **WEB-030 [P2] Final flush relies on pagehide alone; async IDB commit can die
  with the tab** — up to ~5 s of progress lost, exactly the window where
  mission-complete EEPROM writes land; mobile process-kills worst. Fix: also
  persist on `visibilitychange → hidden`; better: debounced (~500 ms)
  event-driven flush after engine /save writes (also fixes the torn multi-file
  snapshot hazard: ini and eeprom captured across an engine write). Effort S.
- **WEB-031 [P2] Two tabs on one origin silently clobber saves** (IDBFS
  last-write-wins per 5 s timer; stale MEMFS in tab B overwrites tab A's
  progress). Fix: `navigator.locks.request("mgb64-game", …)` exclusive at boot;
  warn/refuse a second instance (Web Locks ships in every WebGPU browser).
  Effort S.
- **WEB-032 [P2] No cache-busting/version handshake between `ge007_web.js` and
  `.wasm`** — Pages serves `max-age=600`; around a redeploy a browser can pair
  cached glue with fresh wasm (or vice versa) → cryptic undefined breakage.
  Fix: build_web.sh stamps a short hash; shell loads `ge007_web.js?v=HASH` and
  fetches `ge007_web.wasm?v=HASH` (dynamic injection keeps the 5-file
  invariant). Effort M.
- **WEB-033 [P2] `fetch("ge007_web.wasm")` lacks `response.ok` check** — a 404
  becomes `CompileError: expected magic word` instead of "engine download
  failed (HTTP 404)". Fix: one guard line. Effort S.
- **WEB-034 [P2] CI ROM-magic guard misses byteswapped dumps and
  embedded-at-offset content** (`web-demo.yml:27-36` checks only big-endian
  magic at offset 0; a `.v64`/`.n64` overwriting a whitelisted name passes, as
  does ROM content appended inside the wasm under the 40 MiB budget). Fix:
  check all three magics + scan for the pattern anywhere in each artifact.
  Effort S.
- **WEB-035 [P2] No AudioContext recovery after post-start suspension** (iOS
  interruption: call/Siri/alarm; some BT route changes) — SDL kills its own
  resume timer after first success; zero `visibilitychange`/gesture resume hooks
  anywhere → permanent silence until reload. Also covers the **(H)** initial
  Safari gesture-window concern (context created seconds after the click).
  Fix: shell listeners (`visibilitychange`, `pointerdown`, `keydown`) calling
  `Module.SDL2?.audioContext?.resume()` when not running. Effort S.

### Determinism & wasm longevity
- **WEB-036 [P2] No `-ffp-contract` pinning: native arm64 (FMA) vs wasm (no
  scalar FMA) is a live sim-float divergence channel.** The repo's own header
  says the sim hash is FP-contraction-sensitive; baselines are native-recorded.
  **(H)** — settle by running one deterministic tape in the browser and diffing
  sim hashes vs native Release. Fix: `-ffp-contract=off` on BOTH `ge007` and
  `ge007_web` (re-record baselines once), or document the web sim as a separate
  hash lineage. Effort M.
- **WEB-037 [P2] gfx_ptr registry lifecycle: stale/freed host-pointer entries
  permanently shadow genuine segment tokens; tombstones only accumulate.**
  Only the texture arena ever invalidates (`gfx_ptr.h:107-125`;
  `image.c:163-170`); registry-first resolve (`gfx_pc.c:22320-22325`) means a
  dead heap pointer numerically equal to a later genuine segment token resolves
  into garbage — same "stale registry" class as the Dam door bug, one level up;
  collision surface grows monotonically over a session. Related: tombstones
  never revert to EMPTY (miss-probes lengthen forever → slow-burn frame-time
  creep in multi-hour soaks) and `gfx_ptr_max_probe` isn't updated on
  tombstone-reuse/full-scan inserts (under-reports exactly when aging). Fix:
  invalidate registered ranges at level-teardown (mirror `portClearAllDlCol`)
  or add a per-entry generation stamp reset on stage load; rehash when
  tombstones exceed ~25%; hoist the max-probe update. Effort M.
- **WEB-038 [P2] ILP32 telemetry is invisible on the one target it exists for**
  — `[SEG_ADDR-ILP32]` log is `GE007_DEBUG`-gated and the shell can't set env;
  the five registry counters are written but never read anywhere. Fix: emit a
  one-line counter summary at level transitions/every 3600 frames when nonzero
  (unconditional); let the shell forward `?debug`-style query params into
  `Module.ENV` (also unlocks every GE007_* knob for field debugging — today the
  entire env/CLI diagnostic surface documented across the repo is unreachable
  on web). Effort S.

### Boot & load pipeline
- **WEB-039 [P2] Boot latency: everything starts at Play-click and the big
  awaits are serial** (`mgb64-shell.js:108-110`) — wasm fetch (4 MB), engine JS,
  ROM read strictly sequential, none preloaded, no progress feedback (page goes
  straight to black canvas during fetch+compile+syncfs; users can't distinguish
  a healthy slow boot from the failure modes above). Fix: kick off wasm fetch +
  factory load when the gate passes (`<link rel="preload">` or early fetch);
  `Promise.all` with the ROM read; staged status text ("Downloading engine…",
  "Preparing saves…"); optional streaming-instantiate with arrayBuffer fallback.
  Effort S.

---

## P3 — polish & hygiene

**Input/controls**
- **WEB-040** Suppress right-click context menu on the canvas (aim = RMB; menu
  pops whenever pointer isn't locked). One listener in `boot()`. Effort S.
- **WEB-041** Crouch accepts only `SDLK_c || SDLK_LCTRL` keycodes — RCtrl dead;
  C is layout-keycode while movement is scancode (Dvorak mismatch). Accept
  RCtrl; move crouch into the scancode binding registry. Effort S.
- **WEB-042** Esc-when-never-captured maps to B (interact) via the
  `g_mouseGrabbed` proxy (`stubs.c:6430-6436`) — wrong for keyboard-only players
  and after browser-side lock churn; derive START-vs-B from
  `pcNativeFrontendInputActive()` instead. Effort S.
- **WEB-043** Controls documentation is hand-duplicated in 4 places and has
  already drifted (index.html table, H help, `kGpButtonName` mirrors) — generate
  the index.html fragment from `kDefault/kLabel/kGpDefault/kGpLabel` at build
  time or add a CI grep gate. Effort M.
- **WEB-044** Game-level pointer-lock double-toggle recapture is largely
  redundant with SDL 2.32's built-in regrab — narrow to the desync case or
  comment why both exist. Effort S.

**Audio**
- **WEB-045** Mute (M) is a hard device pause: click on mute + ≤167 ms stale
  audio replay on unmute — replace with a ramped mute gain in
  `portAudioApplyMasterVolume`. Effort S.
- **WEB-046** "[AUDIO] Unified device" log prints the requested spec (22050/512)
  not the real browser device (48 kHz SPN + SDL converting stream) — note the
  conversion in the log under `__EMSCRIPTEN__`. Effort S.
- **WEB-047** Dead callback-mode code (`audioCallback`/`musicMixSamples`,
  `audio_pc.c:942-958`) misleads reasoning about Asyncify reentrancy — delete or
  `#if 0` with a pointer to the queue-mode path. Effort S.
- **WEB-048** `getenv("GE007_TRACE_WEAPON_AUDIO")` in the per-frame mix path —
  cache once like `s_sfxMixLegacy`. Effort S.

**Rendering**
- **WEB-049** Take `caps.formats[0]` (the browser's preferred canvas format)
  instead of forcing BGRA8 when merely supported (Android swizzle cost); set
  `alphaMode = Opaque` explicitly instead of Auto (frame alpha genuinely carries
  sub-1 coverage values behind fences/glass — Auto→premultiplied would bleed the
  page through). Effort S.
- **WEB-050** Shader table overflow (`WGPU_SHADER_MAX` 1024, never reset across
  levels) silently renders new materials with slot 0's combiner after one
  warning — evict LRU instead of aliasing slot 0. Same silent-wrong class the
  DLCOL sweep hunted. Effort M.
- **WEB-051** Diag viewport UBO ring overwrites slot 7 for >8 distinct
  viewports/frame (retroactive rewrite: writeBuffer executes before the command
  buffer) — grow dynamically. Exotic frames only. Effort S.
- **WEB-052** `draw_modern_mesh` creates a UBO + bind group per draw per frame —
  ring of UBOs at dynamic offsets + per-mesh bind-group cache (matters for
  `--showcase` decor tiers). Effort M.
- **WEB-053** Animated/re-uploaded textures destroy + recreate the WGPUTexture —
  `wgpuQueueWriteTexture` into the existing one when dimensions match. Effort S.
- **WEB-054** First-sight pipeline compiles hitch on new materials — pre-warm
  known-hot combiner IDs during level load. Effort M.
- **WEB-055** `gfx_webgpu_imgui.cpp` includes `webgpu/wgpu.h` directly (only TU
  bypassing the compat seam; fails to compile the moment the overlay ships on
  web — prerequisite for WEB-018 long-term). Route through
  `gfx_webgpu_compat.h`. Effort S.

**Wasm/build/docs**
- **WEB-056** Game-defined `sinf/cosf/asinf/acosf/atan2f` are strong symbols
  that also capture SDL's internal libm references under wasm static linking
  (N64-precision tables silently replace musl inside SDL audio/controller
  math) — extend the `math_asinacos.h` rename pattern to the float overrides.
  Effort M.
- **WEB-057** `--debug` documented as SAFE_HEAP/ASSERTIONS but neither flag is
  wired (`build_web.sh` only flips build type) — add
  `-sSAFE_HEAP=1 -sASSERTIONS=2` for Debug link or fix WEB.md. SAFE_HEAP is
  precisely the ILP32 OOB tool this decomp wants. Effort S.
- **WEB-058** Env-gated `sigsetjmp` recovery arms on web but the matching
  `siglongjmp` can never fire (no signals) — mirror the `_WIN32` force-off with
  an "unsupported" notice. Effort S.
- **WEB-059** `configSave()` only runs in `platformShutdownSDL`, which never
  executes on web (no SDL_QUIT, EXIT_RUNTIME=0) — /save/ge007.ini is frozen
  after first boot; bites the moment any web settings surface exists (WEB-018).
  Save opportunistically on change. Effort S.
- **WEB-060** emsdk floor-vs-pin drift: CI pins 4.0.10, build_web.sh accepts
  anything — verify `emcc --version` against the floor and warn/fail. Effort S.
- **WEB-061** `build_web.sh` never cleans `dist/web` — stale local artifacts
  survive rebuilds (`rm -rf` before staging; CI immune). Effort S.
- **WEB-062** Memory footprint: three live 12 MB ROM copies (JS closure never
  nulled, MEMFS, wasm-heap malloc) + `INITIAL_MEMORY=256 MiB` whose itemized
  comment sums nowhere near 256 — null `rom` after boot, unlink the MEMFS copy
  post-init, measure the real high-water mark and drop INITIAL_MEMORY (~128 MiB;
  growth is already on). Mobile-eviction pressure. Effort S.

**Shell UX**
- **WEB-063** Wrong-size re-pick keeps the old stored ROM active while showing
  an error — say "keeping the stored ROM" or clear the input. Effort S.
- **WEB-064** `prefers-reduced-motion` makes the overlay hint pill permanent
  (the fade animation is its only removal path — and it advertises broken keys,
  see WEB-001) — hide via JS timeout. Effort S.
- **WEB-065** `#gate-msg` (capability refusal, "Boot failed") has no
  `role="status"`/`aria-live`; no `<noscript>` fallback. Effort S.
- **WEB-066** Unlock-all checkbox not persisted across visits — mirror to
  localStorage. Effort S.

---

## Suggested execution waves

1. **Ship-safety hotfix** (all S-effort, one afternoon): WEB-001, 003, 004, 009,
   010, 033, 040 + land/deploy WEB-005 (coordinate with the in-flight pacer
   branch). Kills every "user is trapped/tricked" path.
2. **Playability round**: WEB-002, 006, 007, 008, 011, 012, 013, 014, 015 —
   after this the demo is honestly playable end-to-end with correct audio
   semantics and no self-sabotaging keys.
3. **Feel & robustness**: WEB-016–020, 024, 025, 026, 029, 030, 031, 035.
4. **Performance program** (measure first, then fix): WEB-021, 022, 023, 039,
   052, 053, 054, 062 — Chrome profile + Apple-GPU pass-split count + Asyncify
   flag experiment, validated by the tape gate.
5. **Determinism & longevity**: WEB-036, 037, 038, 050, 056 — browser tape/hash
   parity run, soak test, registry lifecycle.
6. **Polish & guardrails**: everything else in P3, plus WEB-032, 034, 043
   (drift gate), 057, 060.

## Verification gates to add along the way

- One deterministic tape replayed in the browser with sim-hash diff vs native
  (settles WEB-036, guards WEB-022's flag experiment).
- A 2-hour browser soak logging `gfx_ptr` occupancy/max-probe (WEB-037).
- Chrome performance profile of one gameplay frame (WEB-023 baseline).
- Playwright boot smoke (gate → ROM → first frame → F-key audit) — previously
  deferred YAGNI; wave 1 touches enough shell code to justify it now.

## Cross-lane "verified clean" highlights (don't re-audit)

- Memory-blend WGSL math is term-for-term identical to GL (blender bias,
  coverage rounding, Y-flip pixel-exact); no readbacks anywhere in the port.
- Frame pacing resyncs cleanly after background-tab suspension (no catch-up
  spiral, `FRAME_SPIKE_CAP 4`); stall watchdog fully compiled out on web.
- Release wasm link flags clean (-O3, no debug artifacts, SIMD on, no threads →
  no COOP/COEP need); one encoder + one submit per frame; 16 MB persistent
  vertex bump-allocator.
- ROM byteswap (.v64/.n64) genuinely handled engine-side; OPFS writes atomic;
  EEPROM writes synchronous into MEMFS; config ini written via tmp+rename.
- WASD are scancodes (AZERTY/Dvorak-safe); wheel weapon-switch preventDefaulted
  with correct pixel-mode scaling; SDL resets keyboard on blur (no stuck keys);
  gamepad hotplug + Standard-Gamepad mapping work under the press-to-expose model.
- Audio underruns fill silence (no looping garbage) at both SDL layers; no
  malloc/locks in the audio frame path; background-tab audio drains to clean
  silence and resyncs.
- random.c/math_asinacos wasm branches are sim-identical (integer paths);
  deterministic seed identical; no TLS/threads in the web link; byteswap.h
  endianness-correct; `gfx_ptr_store` itself is lossless (explicit else-path
  counters — NOT the Dam-door pattern).
- web-demo.yml: workflow_dispatch-only, SHA-pinned actions, minimal permissions,
  5-file whitelist fails closed, size gate correct.
