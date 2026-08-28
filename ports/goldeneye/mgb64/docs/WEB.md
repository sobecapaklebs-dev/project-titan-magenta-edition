# MGB64 in the browser

A static-page build of the same engine (`ge007`) that ships as the desktop
apps, compiled to a single wasm binary and served from GitHub Pages. Same
disclaimer as everywhere else in this repo: **you supply your own legally-owned
ROM.** The site ships engine + page only — no ROM or ROM-derived data is ever
built, packaged, or hosted. See [../DISCLAIMER.md](../DISCLAIMER.md).

---

## User flow

### Browser support

The demo requires **WebGPU**. There is deliberately no WebGL fallback — a
capability gate shows a friendly message instead of a degraded experience.

| Browser              | Supported | Notes                                   |
|----------------------|-----------|------------------------------------------|
| Chrome / Edge 113+   | Yes       | WebGPU shipped by default.               |
| Safari 26+           | Yes       | WebGPU shipped by default (macOS/iOS).   |
| Firefox (Windows) 141+ | Yes     | WebGPU behind default-on as of 141.      |
| Firefox (Linux/Android) | **No**  | WebGPU not yet shipped on these platforms. |
| Any browser without WebGPU | **No** | Gate message, no fallback renderer.   |

This combination reaches roughly **82% of global browser share** (per
caniuse WebGPU data at plan time). The `No` rows are not "degraded" — the
page detects `"gpu" in navigator` and `navigator.storage.getDirectory` up
front and refuses to boot with an explanatory message rather than attempting
a broken run.

### Your ROM never leaves your browser

1. You pick a GoldenEye 007 (U) `.z64` ROM file (exactly 12 MB) via the file
   input on the page.
2. The bytes are copied into your browser's **Origin-Private File System
   (OPFS)** — storage that is private to this site's origin and invisible to
   any server. **The ROM is never uploaded anywhere; there is no server-side
   component that could receive it.** Reloading the page reads the ROM back
   out of OPFS so you don't have to pick the file again.
3. A **"Forget stored ROM"** button removes the OPFS copy immediately.

### Storage and quota

- OPFS storage is subject to the browser's normal site-storage quota. The
  page calls `navigator.storage.persist()` (best-effort; not all browsers
  grant it) after a successful store to reduce the odds of eviction under
  disk pressure.
- **Store failure does not block play.** If the OPFS write fails (quota
  exceeded, private-browsing restrictions, etc.), the page still boots the
  game from the in-memory ROM bytes you just picked — you'll simply be asked
  to pick the file again on your next visit. This is a deliberate
  degrade-gracefully choice: a storage failure is never a play blocker.
- Saves auto-persist via **IDBFS** mounted at `/save`: the in-memory
  Emscripten filesystem is flushed to IndexedDB on a 5-second timer and again
  on the `pagehide` event (covers both "still open" and "tab closing"
  exits). There is no explicit "save now" button — it's automatic, like the
  desktop build's save-on-exit behavior, just more frequent because a browser
  tab can be discarded without warning.
- **Save round-trip has been owner-verified**: play, let the timer or a
  pagehide fire, reload the page, confirm progress is intact. This is a
  manual verification step on the owner's release checklist (see
  RELEASING.md) — it is not (yet) covered by an automated browser gate (see
  "Known items" below).

---

## Developer flow

### Install emsdk

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 4.0.10
~/emsdk/emsdk activate 4.0.10
```

**4.0.10 is a floor, not a pin** — it's the first emsdk release that bundles
the `emdawnwebgpu` port this build depends on for the WebGPU bindings.
`tools/web/build_web.sh` checks for `$EMSDK_DIR/emsdk_env.sh` and prints the
install commands above if it's missing.

### Build

```sh
tools/web/build_web.sh            # Release (default)
tools/web/build_web.sh --debug    # Debug (SAFE_HEAP=1 + ASSERTIONS=2 on)
```

This configures an Emscripten-toolchain build in `build-web/` (kept separate
from the native `build/` tree — different toolchain, different CMake cache),
builds the `ge007_web` target, and stages the deployable site into
`dist/web/`: `ge007_web.js`, `ge007_web.wasm`, plus the static shell
(`web/index.html`, `web/mgb64-shell.js`, `web/style.css`, `web/sw.js`) copied
alongside. Both `build-web/` and `dist/` are gitignored — they're build output,
not source.

**The dist is exactly 6 files** (the two engine files + four static shell
files). This count is an enforced invariant: the source list here, the
`build_web.sh` staging + hash-stamp step, and the `web-demo.yml` ROM-absence
whitelist (which fails closed on any unexpected file) must all stay in lockstep.
PERF-036 grew it from 5 to 6 by adding `sw.js`; the runtime-generated favicon is
deliberately NOT a file (see `mgb64-shell.js`), so it does not count.

### Serve it locally

```sh
tools/web/serve_web.sh            # serves dist/web on http://127.0.0.1:8000
tools/web/serve_web.sh 9000        # optional port override
```

A plain static file server is sufficient — there is no backend. WebGPU
requires a secure context; `127.0.0.1` counts as one, so plain HTTP against
localhost works fine for local iteration.

### Browser regression lane

Two headless-Chrome lanes guard the browser build (both zero-npm-dep,
CDP-over-pipe, both `LABELS "web"`, both self-skip with exit 125 when
dist/ROM/Chrome/node is absent):

- **`ctest -R web_boot_smoke`** — boot end-to-end and prove the backend reaches
  a live, non-black rendering state. Cannot see an *incomplete* frame.
- **`ctest -R web_frame_probe`** — reach REAL gameplay in a level and assert
  frame **completeness**. This exists because a non-black check is blind to the
  PERF-005 "sky-flood" bleed (a single colour flooding the viewport under load);
  boot-smoke would have passed a flood frame.

The probe (`tools/web/web_frame_probe.sh` → `tools/web/webcap.mjs`) runs **two
passes, both of which must pass**:

1. **Deterministic dam** — boots `?arg=--level&arg=dam&arg=--deterministic` and
   asserts a complete frame. `--deterministic` disables the PERF-005 async path,
   so this is a clean, reproducible level frame with no input.
2. **Live + CPU-throttled dam** — boots `?arg=--level&arg=dam` (no determinism)
   under `--throttle 4` with a held-forward move, bursting screenshots through
   the movement-under-load window. This is the pass that would have *caught* the
   bleed. Because PERF-005b **holds incomplete frames** (the canvas keeps the
   last complete image), it must only ever see the gate screen or complete
   gameplay — never a flood. That invariant is the regression contract.

The completeness check (`--assert-complete T`, default `T=90`) decodes a
screenshot and fails if the single most common exact RGB triple covers more than
`T`% of pixels — a real gameplay frame is richly varied; a flood is ~one colour.
It also fails on a visible `#fatal` panel or a "Boot failed" gate message.

`webcap.mjs` is a standalone tool (no SKIP semantics — the wrapper gates deps).
Beyond the probe it takes `--keys` (scripted holds/taps, e.g.
`hold:KeyW:2000;tap:Space`), `--burst N:M` (N screenshots every M ms for flicker
hunts), `--settle-ms`, `--window`, and `--query` — see its header for the full
grammar.

#### `?arg=` URL passthrough

Both lanes depend on a small dev/test surface in `web/mgb64-shell.js`: every
`?arg=<value>` in the page URL is appended to the engine's `callMain` args
(after the shell's own defaults, so a query arg can override them). This is how
the probe boots straight into a level (`?arg=--level&arg=dam`). It needs no
gating — the browser build is a BYO-ROM app with no server, so a query arg only
ever affects the user's **own** in-tab session, the same trust domain as typing
into the devtools console. The same passthrough backs the web input-tape
harness.

### Service worker (repeat visits + offline) — PERF-036

`web/sw.js` is a **hash-versioned, cache-first** service worker. On first visit
it precaches the whole app (`./`, `index.html`, `style.css`, `mgb64-shell.js`,
`ge007_web.js`, `ge007_web.wasm` — everything except sw.js itself, which the
browser fetches through its own update channel). After that, repeat visits boot
**from cache** (instant, no re-download and no 10-minute Pages re-validation),
and the demo is **fully playable offline** — the ROM is already in OPFS and
saves in IDBFS, so nothing needs the network.

- **Hash-keyed cache generation.** `CACHE_NAME = "mgb64-" + BUILD`, where `BUILD`
  is the same `__MGB64_BUILD__` stamp `build_web.sh` rewrites in the shell —
  each deploy is its own independent cache.
- **Atomic activate-on-complete.** `install` calls `skipWaiting()` *only after*
  `cache.addAll()` resolves, so a half-downloaded new build never takes over from
  a good old one. `activate` deletes every *other* `mgb64-*` cache (old deploys)
  and claims open clients. This closes the WEB-032 glue/wasm skew window more
  robustly than `?v=`: the whole app is one atomic generation, so a cached
  index/shell can never pair with wasm from a different build. An open tab on an
  old build switches to the new one on its next navigation.
- **`?v=HASH` interaction.** The shell requests the engine files as
  `ge007_web.js?v=HASH` / `ge007_web.wasm?v=HASH`. They are precached under their
  canonical query-less URLs and matched with `{ ignoreSearch: true }`, so a `?v=`
  request still hits the cache. `ignoreSearch` only ignores the query (paths
  still match exactly), which also lets a dev/test navigation carrying `?arg=…`
  (the URL passthrough above) resolve to the cached shell offline.
- **Scope of interception.** Same-origin `GET` only, cache-first with network
  fallback; anything not precached is a plain network passthrough. Cross-origin
  and non-GET requests are never touched. The worker logs nothing (the boot-smoke
  console gate fails on unexpected console output).
- **Unstamped-source gate + ctest behaviour.** Registration in the shell is gated
  on the *stamped-build* check (`BUILD.indexOf("__MGB64") === -1`, the same one
  `?v=` uses). The raw `web/` source keeps the literal placeholder, so an
  un-built copy never installs a worker — a defensive guarantee, not a real boot
  path (raw source has no built engine files anyway). A `build_web.sh`-produced
  `dist/web` **is** stamped, so both `serve_web.sh` and the `web_boot_smoke` /
  `web_frame_probe` ctest lanes serve a stamped shell and **the worker does
  register there.** That is fine and is what the lanes cover: each runs in a
  **fresh Chrome profile** (a clean install every run, so it's deterministic — no
  cache carries over), first-load content comes from the local server, and the
  only assertion is that install-does-not-break-boot (registration is
  fire-and-forget on `load`, so it never competes with or blocks the engine
  download). Manual-dev caveat: `BUILD` is the git `HEAD` short hash and is stable
  across same-commit rebuilds, so if you iterate on a persistent browser profile,
  unregister the worker (DevTools → Application → Service Workers) or hard-reload
  to pick up edits.

### The compat-seam rule

**All WebGPU dialect divergence between native (`wgpu-native`) and browser
(`emdawnwebgpu`) goes through `src/platform/fast3d/gfx_webgpu_compat.h`.**

`gfx_webgpu.c` itself must never contain an inline `#ifdef __EMSCRIPTEN__`.
Instead, the compat header supplies the seam: event pumping, blocking waits,
and surface creation are each given one name that means the same thing on
both targets, with the `#ifdef` fork living entirely inside the header. This
keeps `gfx_webgpu.c` a single, readable code path shared by every platform
instead of accumulating divergent branches inline. If you find yourself about
to add `#ifdef __EMSCRIPTEN__` directly in `gfx_webgpu.c`, that's a signal the
seam is missing a primitive — add it to the compat header instead.

### Audio output backend (WEB-069)

The browser build has **two** audio output paths, chosen at startup in
`portAudioInit` (`src/platform/audio_pc.c`):

1. **AudioWorklet (preferred)** — `src/platform/web_audio_worklet.c`. A plain-JS
   `AudioWorkletProcessor` (injected as a Blob module, so no 7th deploy file)
   owns a ring buffer and pulls 128-sample render quanta (~2.7 ms) on the audio
   render thread. The engine still synthesises on the main thread (it is welded
   to the 60 Hz sim tick) and posts finished PCM into the ring via
   `postMessage`. The occupancy controller reads ring depth synchronously on the
   main thread from the worklet's last reported count plus `ctx.currentTime`, so
   this needs **no SharedArrayBuffer, no cross-origin isolation, and no headers**
   — it works on plain GitHub Pages. It does **not** touch the WebGPU build.
2. **SDL ScriptProcessorNode (fallback)** — if the browser has no AudioWorklet,
   `webAudioOutputInit` returns 0 and `portAudioInit` opens the legacy SDL device
   (a main-thread ScriptProcessorNode with a deep 2048-sample buffer).

**Why it matters.** The SDL SPN path runs *both* the `SDL_QueueAudio` pump and
the audio callback on the main thread, so a GC pause / WebGPU pipeline compile /
level-load stall starves audio — which forced a deep (~43 ms) device buffer as
glitch insurance and made the fire-to-hear delay ~2× native. Moving the drain to
the worklet's audio thread removes that: measured device-side latency drops from
~136 ms (queue + SPN buffer) to ~58 ms (ring only) — **device-side native
parity** (the web pipeline keeps a deliberately deeper main-thread queue
target, ~93 ms mean vs native ~58 ms end-to-end; see FID-0141) — with
zero steady-state underruns. Big main-thread stalls (level load) still glitch on
*either* path; during those the sim is paused anyway. See FID-0141 / WEB-069.

The swap is a leaf at the `SDL_QueueAudio` boundary: `osAiSetNextBuffer` /
`osAiGetLength` / `osAiQueueBelowLimit` in `stubs.c` route through the worklet
via `ai_enqueue()` / `ai_queued_bytes()` when `webAudioOutputActive()`. The
synth, SFX mixer, VADPCM decoder, mute ramp, and master-volume stages are all
upstream and unchanged. Native is completely unaffected (real SDL + CoreAudio /
WASAPI). The worklet reuses `Module.SDL2.audioContext` so the shell's existing
gesture-resume / teardown lifecycle (WEB-035 / WEB-009) works unchanged.

### ILP32 (wasm32) notes

wasm32 is an ILP32 target: pointers are 32 bits, same width as the N64's
native 32-bit segment-relative addresses used throughout the RDP display-list
decoding. That means a bare 32-bit value flowing through the renderer's
token-resolution paths is ambiguous between "N64 segment token" and "host
pointer" in a way it never was on a 64-bit native build (where a real host
pointer can't collide with a 32-bit token).

- **Registry-authoritative discrimination**: the fix is `gfx_ptr_resolve()` —
  a small open-addressed registry (`gfx_ptr_keys`/`gfx_ptr_vals`/
  `gfx_ptr_state`, declared in `gfx_ptr.h`) that authoritatively maps
  registered host pointers back to their 32-bit tokens. A 32-bit value is only
  ever treated as a bare N64 segment address as a *fallback*, after the
  registry lookup misses.
- **`[SEG_ADDR-ILP32]` telemetry** (`gfx_pc.c`, the registry-miss path): when a
  token misses the registry and gets resolved via segment-table fallback
  instead, the first 5 occurrences per boot log a diagnostic line. This is a
  coverage signal, not a hard failure — it means "this token was either a
  genuine N64 segment address, or a host pointer that never got registered
  (a gap in registry coverage)." It never gates the sim; it's a watch-item.
- **`gfx_ptr` longevity watch-item**: the registry is a fixed-size table
  (`GFX_PTR_TABLE_SIZE`) with insertion-coverage that depends on every host
  pointer that might be walked back through the RDP path being registered at
  creation time. If a future edit adds a new pointer-bearing structure to the
  hot path without registering it, the wasm build will silently fall back to
  segment-table resolution for it — functionally survivable (per the design)
  but worth checking the `[SEG_ADDR-ILP32]` log line count if wasm rendering
  ever looks subtly wrong after a renderer-adjacent change.
- **Fixed 2026-07-16 (`68afbc6`, review-hardened in `4494a63`): mission-load
  crash class.** Two ILP32-only faults surfaced when actually loading a
  mission (not just booting to menu):
  1. A dynAllocate-pool host pointer (matrix/vertex/light/lookat/viewport/
     sub-DL) missed the `gfx_ptr` registry and got mis-routed through a live
     N64 segment to a garbage address, faulting in `gfx_sp_matrix`. Fixed by
     resolving any address that falls inside the PC dynamic DL/VTX arenas
     (`gfx_addr_is_pc_host_pool`, ranges tracked by `dyn.c`) directly as a
     host pointer *before* segment-table lookup, closing the whole
     dynAllocate* class at once (this also silently blanked the animated
     menu/folder-select character models).
  2. Boot logos (Rare/RareWare/Nintendo/N64) went missing because
     `gfx_is_static_pc_dl`'s "near program base" 64MB window — valid on LP64,
     where static data sits far from the heap — false-positived on wasm
     (static rodata and heap share one linear memory), swallowing
     heap-resident N64-registered ROM logo DLs into the host-endian PC
     interpreter path. Fixed by treating any explicitly
     `gfx_register_n64_dl_region`'d address as never-a-static-PC-DL on
     ILP32 — the static-DL window guard now defers to the N64-DL registry
     first.
  Both fixes are width-guarded (LP64/native is untouched). `4494a63` also
  makes the registry-miss/unresolvable-token counter unconditional (log line
  stays `GE007_DEBUG`-gated) so a future coverage gap shows up in release
  telemetry even without debug logging on.

### Sim-hash lineage (WEB-036 ruling)

**The wasm sim is a separate hash lineage from native.** Native arm64 Release
contracts eligible float expressions to FMA (`-ffp-contract=on` is the clang
default and the target has `fmadd`); core wasm has no scalar FMA instruction,
so identical C can round differently on long float chains. The repo's own
sim-state-hash lanes are documented as FP-contraction-sensitive, and all
recorded baselines are native.

Considered and REJECTED: pinning `-ffp-contract=off` on both targets. That
would (a) cost native FP performance across the whole sim (FMA disabled) and
(b) force re-recording every recorded baseline — all to buy a cross-target
bit-equality that no player observes (the browser sim is internally
deterministic and self-consistent; only a native-recorded tape replayed in the
browser would notice).

Consequences of the ruling:
- Never assert a native-recorded sim hash against a browser run (or vice
  versa). Tape *inputs* are portable; hash *baselines* are per-lineage.
- A future browser tape lane records its own `.expected` hashes via the
  headless smoke harness. Divergence *within* the web lineage is a real
  regression; divergence *across* lineages is expected physics.
- If cross-target bit-equality ever becomes a requirement (e.g. cross-platform
  netplay), revisit with a measured `-ffp-contract=off` perf cost and a
  one-time baseline re-record — an owner decision.

### wasm size budget

CI enforces a **40 MiB hard ceiling** on `dist/web/ge007_web.wasm` (GitHub
Pages' own per-file limit is 100 MiB; 40 MiB is this project's own
fail-closed budget, checked in `web-demo.yml`). Current actual size measured
from a clean build at this task's HEAD:

```
dist/web/ge007_web.wasm: 3,991,939 bytes (~3.81 MiB)
```

— comfortably under budget. Re-measure after any change that meaningfully
grows the linked code (`stat -f%z dist/web/ge007_web.wasm` on macOS,
`stat -c%s` on Linux — the CI step uses the latter).

---

## Deploy flow

Deploys are **owner-triggered only** — `web-demo.yml` is
`workflow_dispatch`-only, matching this repo's guarded-publish, no-hosted-
auto-triggers doctrine. Nothing pushes to `main` and auto-deploys the demo.

### One-time repo setup

**Settings → Pages → Source → GitHub Actions.** This must be selected once
per repo (or per fork) before the workflow can deploy — it tells GitHub Pages
to accept artifacts from a workflow run instead of serving a branch.

### Dispatching a deploy

The owner runs `web-demo.yml` via `workflow_dispatch` (Actions tab, or `gh
workflow run web-demo.yml`). The job builds with emsdk 4.0.10, checks the
wasm size budget, runs a ROM-absence guard over the exact artifact set (fails
closed if anything unexpected — or anything starting with the N64 ROM magic
bytes — shows up in `dist/web/`), then uploads and deploys via
`actions/deploy-pages`.

### `id-token` posture

The workflow requests `permissions: id-token: write`. This is **not** a
general-purpose credential — it's the standard OIDC token
`actions/deploy-pages` requires to authenticate its deployment to GitHub
Pages, scoped by GitHub to that single purpose for this workflow run. The job
otherwise only holds `contents: read` and `pages: write`; it has no push
access to the repo and no ability to mint tokens usable outside the Pages
deploy step.

---

## Known items

- **Firefox on Linux/Android is unsupported** — WebGPU isn't shipped there
  yet. The capability gate declines gracefully rather than attempting a
  broken run; there is no fallback renderer by design (see "Browser support"
  above).
- **DAM-R1 (top post-release item)**: an over-bright rectangular sky/backdrop
  seam at portal boundaries on Dam (intro swirl ~frame 190 + gameplay from the
  dam walkway) — **WebGPU-only** (GL renders it correctly; decisive frame A/B
  captured), survives the camera-seed-walk / xlu-cvg / sky-depth toggles, so
  it's a backend blend/fog defect with mechanism not yet traced. Likely what
  reads as "water/sky bleeding through" in play. Repro + evidence + resume
  commands: docs/audit/DAM_PARITY_HUNT_2026-07-17.md.
- **Dam legacy parity residuals (deferred, owner-acknowledged)**: the
  establishing-shot distant-shore drop (DAM-R2, same on every backend — the
  M3.4/T25 room-admission family, stock-N64 verdict pending) and the
  un-swept interior/outro areas. Adjudication bar = stock capture (if the
  hardware shows it, it's faithful). Same hunt document. Notably the literal
  "water through geometry" could NOT be reproduced — reservoir water occludes
  correctly on both backends everywhere inspected.
- Deferred engineering items (measurement- or surface-gated) are enumerated
  with owners in docs/WEB_BACKLOG.md's status ledger: in-game settings overlay
  on web (WEB-018-full), Asyncify bounding experiment (WEB-022), pass-split
  coalescing (WEB-021res), combiner attr packing (WEB-028, armed diagnostic),
  shader-eviction gfx_pc hook (WEB-050b, conditional), pipeline prewarm
  (WEB-054). The wasm sim-hash lineage ruling (WEB-036) is documented above.

### Resolved (kept for discoverability)

- **RenderScale blocky artifacting** — root cause was the WebGPU DEFAULT
  device limit (8192) plus a hardcoded cap, not hardware: fixed by requesting
  the adapter's real maxTextureDimension2D (verified live: 16384 granted).
- **Menu glyph corruption** ("SELECT"→"SYLYCT") — bind-group cache
  handle-reuse ABA, fixed with cache invalidation on view/BGL release
  (`d820ff1`); the deterministic menu frame-dump repro is the regression
  check.
- **Automated browser gate** — no longer deferred: `ctest web_boot_smoke`
  (headless-Chrome, zero-dependency CDP) walks gate → ROM → boot → live
  frames → non-black screenshot → clean console. SKIPs without dist/ROM/
  Chrome; LABELS web.
- The wasm32 mission-load crash class (dyn-pool host-pointer seg-alias +
  static-DL window false-positive on boot logos) — see "ILP32 (wasm32) notes"
  above (`68afbc6`, `4494a63`).
