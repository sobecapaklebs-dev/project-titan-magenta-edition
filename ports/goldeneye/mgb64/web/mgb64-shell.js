// mgb64-shell.js — static-page shell for the MGB64 wasm engine.
// Storage design: ROM bytes -> OPFS (persist once, reseed each visit);
// saves/config -> IDBFS mounted at /save (FS.syncfs on a timer + pagehide).
//
// Engine load: the `ge007_web` link options (-sMODULARIZE=1
// -sEXPORT_NAME=createMGB64, no -sEXPORT_ES6) emit emscripten's default
// UMD-style factory — it ends with
// `if (typeof exports === 'object' ...) module.exports = createMGB64; ...`
// and otherwise falls through to a bare `var createMGB64 = ...` in whatever
// scope it is evaluated in. There is no `export` statement for `import()` to
// bind to, so this shell injects ge007_web.js as a classic script tag and
// reads the resulting `window.createMGB64` global rather than using dynamic
// `import()`. index.html therefore also loads *this* file as a classic script
// (no `type="module"`), since nothing else here needs ESM semantics.
const ROM_OPFS_NAME = "baserom.z64";
const ROM_SIZE = 12 * 1024 * 1024;

// WEB-018: web has no in-game settings overlay (MGB64_APP OFF) or editable ini,
// so the shell exposes two input prefs (see index.html) and passes them as
// --config-override Input.* args at boot. The sensitivity slider is a MULTIPLIER
// of the engine's default mouse sensitivity (Input.MouseSensitivity default 0.15,
// registered in platform_sdl.c), so 1.00× reproduces native feel and the slider
// range 0.25×–3.00× maps to 0.0375–0.45 — well inside the registered 0.01–2.0
// clamp. This factor is INDEPENDENT of WEB-016's browser-scaling normalization
// (that undoes SDL's window-size-dependent coordinate rescale so feel matches
// native; this is the user's separate preference); the two multiply.
const MOUSE_SENS_DEFAULT = 0.15;

// WEB-032: cache-busting handshake between the glue script and the wasm. Pages
// serves both with max-age, so around a redeploy a browser can pair cached glue
// with fresh wasm (or vice versa) → cryptic "undefined"/CompileError breakage.
// build_web.sh seds this placeholder to a short content hash in the STAGED dist
// copy only; the untouched source keeps the literal. When substituted we suffix
// ?v=HASH on BOTH the ge007_web.js <script> and the ge007_web.wasm fetch, so the
// pair always matches. PERF-036 also keys the service-worker cache generation on
// this same stamp (see registerServiceWorker below). Dynamic injection keeps the
// favicon out of the dist, which is exactly 6 files (5 static + sw.js).
const BUILD = "__MGB64_BUILD__";
const _verParam = BUILD.indexOf("__MGB64") === -1 ? ("?v=" + BUILD) : "";
// PERF-036: true only for a build_web.sh-STAMPED copy (the `?v=` case above). An
// unstamped dev/ctest tree keeps the literal placeholder — SW registration is
// gated on this so quick-iterating dev servers and the browser regression lanes
// never get a sticky worker serving a stale iteration.
const _isStampedBuild = BUILD.indexOf("__MGB64") === -1;

// Favicon (trademark-free crosshair in the page accent), generated at runtime
// as a Blob URL. Deliberately NOT a tracked data: URI (contamination guard
// forbids embedded data-URI payloads) and NOT an extra dist file (the web-demo
// workflow pins the Pages artifact to exactly 6 files: the 5 static shell files
// plus sw.js). Kills /favicon.ico 404.
(() => {
  const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">'
    + '<g stroke="#5ce09a" stroke-width="2" fill="none">'
    + '<circle cx="16" cy="16" r="9"/>'
    + '<line x1="16" y1="2" x2="16" y2="9"/><line x1="16" y1="23" x2="16" y2="30"/>'
    + '<line x1="2" y1="16" x2="9" y2="16"/><line x1="23" y1="16" x2="30" y2="16"/>'
    + '</g><circle cx="16" cy="16" r="2.5" fill="#5ce09a"/></svg>';
  const link = document.createElement("link");
  link.rel = "icon";
  link.href = URL.createObjectURL(new Blob([svg], { type: "image/svg+xml" }));
  document.head.appendChild(link);
})();

// PERF-036: register the cache-first service worker (web/sw.js) for instant
// repeat boots + full offline play, and a more robust close on the WEB-032
// glue/wasm skew window (atomic activate-on-complete keyed on the same build
// hash). Deliberately fire-and-forget on `load`, so it NEVER competes with the
// boot-critical engine download the main IIFE below kicks off, and never blocks
// boot. Feature-checked + try/catch + silent .catch so a missing or blocked SW
// is a non-event — the demo runs identically without it. No updatefound/reload
// prompt: a new build precaches and activates atomically, and the next
// navigation picks it up on its own (keep it simple).
//
// Gated on _isStampedBuild: an UNSTAMPED copy (the raw web/ source, placeholder
// intact — never a real boot, since it lacks the built engine files) never
// installs a worker. A build_web.sh-produced dist IS stamped, so serve_web.sh
// and the web_boot_smoke / web_frame_probe ctest lanes DO register it — which is
// fine: the lanes use a fresh Chrome profile (clean install every run) and only
// assert that install doesn't break boot; registration is fire-and-forget so it
// never blocks the engine download. (Manual dev caveat: BUILD is the git HEAD
// short hash, stable across same-commit rebuilds, so if you iterate with a
// persistent browser profile, unregister the worker / hard-reload to see edits.)
if (_isStampedBuild && "serviceWorker" in navigator) {
  addEventListener("load", () => {
    try { navigator.serviceWorker.register("sw.js").catch(() => {}); } catch {}
  });
}

const $ = (id) => document.getElementById(id);

async function opfsRoot() { return await navigator.storage.getDirectory(); }

async function loadStoredRom() {
  try {
    const dir = await opfsRoot();
    const fh = await dir.getFileHandle(ROM_OPFS_NAME);
    const f = await fh.getFile();
    if (f.size !== ROM_SIZE) return null;
    return new Uint8Array(await f.arrayBuffer());
  } catch { return null; }
}

// Returns true on success. Storing is a convenience (persist across visits),
// not a requirement — callers must still let the user boot from the in-memory
// bytes even when this fails (e.g. OPFS quota exceeded).
async function storeRom(bytes) {
  try {
    const dir = await opfsRoot();
    const fh = await dir.getFileHandle(ROM_OPFS_NAME, { create: true });
    const w = await fh.createWritable();
    await w.write(bytes); await w.close();
    try { await navigator.storage.persist(); } catch {}
    return true;
  } catch (e) {
    console.error("[shell] storeRom failed:", e);
    return false;
  }
}

async function forgetRom() {
  const dir = await opfsRoot();
  try { await dir.removeEntry(ROM_OPFS_NAME); } catch {}
}

// WEB-003: `"gpu" in navigator` is NOT enough. Adapter-less configurations
// (Chrome on Linux defaults, blocklisted/disabled GPUs, some Android) expose
// navigator.gpu but resolve requestAdapter() to null — those users would pass a
// synchronous gate, store a ROM, hit Play, and boot to a permanently black
// canvas (sim/audio running against nothing). Requiring a real adapter here
// turns that trap into an up-front, honest refusal. Async as a result.
async function gate() {
  if (!("gpu" in navigator))
    return "This demo needs WebGPU (Chrome/Edge 113+, Safari 26+, Firefox 141+ on Windows). Your browser doesn't support it yet.";
  if (!("storage" in navigator) || !navigator.storage.getDirectory)
    return "This demo needs Origin-Private File System storage, which your browser doesn't support.";
  try {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter)
      return "Your browser exposes WebGPU but no compatible GPU is available (it may be blocklisted or disabled in your browser/OS settings). The game can't render here.";
  } catch (e) {
    return "WebGPU adapter request failed (" + e + "). The game can't render here.";
  }
  return null;
}

// WEB-003 / WEB-010 / WEB-025: single human-readable fatal-error surface. The C
// WebGPU bring-up failure path calls this via window.mgb64ShowError (EM_ASM in
// gfx_webgpu_compat.h), and the global crash handlers below route through it
// too. First message wins; it hides the canvas so the user sees text, not an
// inert black frame.
let _fatalShown = false;
function showFatal(msg) {
  if (_fatalShown) return;
  _fatalShown = true;
  const c = $("canvas"); if (c) c.hidden = true;
  const hint = $("overlay-hint"); if (hint) hint.hidden = true;
  let panel = $("fatal");
  if (!panel) {
    panel = document.createElement("div");
    panel.id = "fatal";
    panel.setAttribute("role", "alert");
    panel.style.cssText =
      "position:fixed;inset:0;display:flex;align-items:center;justify-content:center;" +
      "padding:2rem;text-align:center;font:500 1.1rem/1.5 system-ui,sans-serif;" +
      "color:#e6f2ea;background:#0b0f0d;z-index:9999;";
    document.body.appendChild(panel);
  }
  panel.textContent = msg;
  panel.hidden = false;
}
// WEB-009 interplay: a fatal shown during a FAILED boot (onAbort/bring-up
// failure) must not permanently cover the recovered gate UI — boot().catch
// calls this to drop the panel and re-arm the latch so a retry can surface a
// fresh failure.
function hideFatal() {
  _fatalShown = false;
  const panel = $("fatal"); if (panel) panel.hidden = true;
}
// Exposed for the C bring-up/device-lost path (see gfx_webgpu_compat.h).
window.mgb64ShowError = (msg) =>
  showFatal(msg || "The graphics device failed to start. Reload to try again.");

// Loads ge007_web.js as a classic script (see file-header ADAPTATION note)
// and returns the `createMGB64` factory it installs on `window`.
let _enginePromise = null;
function loadEngineFactory() {
  if (window.createMGB64) return Promise.resolve(window.createMGB64);
  if (_enginePromise) return _enginePromise;
  _enginePromise = new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = "ge007_web.js" + _verParam;  // WEB-032: version-pin with the wasm
    s.onload = () => {
      if (window.createMGB64) resolve(window.createMGB64);
      else reject(new Error("ge007_web.js loaded but did not define createMGB64"));
    };
    s.onerror = () => reject(new Error("failed to load ge007_web.js"));
    document.head.appendChild(s);
  });
  return _enginePromise;
}

// WEB-039 / WEB-033: stream the wasm straight into compilation. The old path
// buffered the whole binary to an ArrayBuffer and handed it over as `wasmBinary`,
// so compile could not start until the last byte landed — the stated reason was a
// fear that Pages misserves .wasm's MIME, but Pages actually sends
// `application/wasm`. Now Module.instantiateWasm (see boot()) feeds
// WebAssembly.instantiateStreaming a live Response so download and compile
// overlap; hosts that DO misserve the MIME (or lack instantiateStreaming, or are
// a plain local dev server) are covered by the arrayBuffer fallback below.
// fetchWasm() therefore yields the Response itself, not the bytes, and is kicked
// off BEFORE the gate so the download overlaps requestAdapter().
let wasmPromise = null;
function fetchWasm() {
  return fetch("ge007_web.wasm" + _verParam).then((resp) => {
    // WEB-033: without this an HTTP 404/500 becomes a cryptic
    // "CompileError: expected magic word" instead of a clear download failure.
    if (!resp.ok) throw new Error(`engine download failed (HTTP ${resp.status})`);
    return resp;
  });
}

// Emscripten Module.instantiateWasm hook. Contract: begin instantiation, return
// {} synchronously, and call successCallback(instance, module) once ready. We
// stream from the in-flight Response (wasmPromise, kicked off before the gate),
// lazily starting a fetch if a WEB-009 retry cleared it. `onError` reject-routes
// an ASYNC instantiation failure back into boot()'s await — Emscripten does NOT
// surface a rejected instantiateWasm on its own (an uncaught failure would just
// hang the factory), and boot() must still see a fetch blip to drive the WEB-009
// transient-reset the same way the old `await wasmBinary` fetch error did.
function makeInstantiateWasm(onError) {
  return (imports, successCallback) => {
    const streamable = wasmPromise || (wasmPromise = fetchWasm());
    // Fallback for hosts lacking instantiateStreaming or misserving the .wasm MIME
    // (and local dev servers). instantiateStreaming has already drained the
    // Response body, so re-fetch fresh bytes rather than reusing it. WEB-033 is
    // re-checked inside fetchWasm(), so a 404 still surfaces as the clear download
    // error (not a compile error). Logged at log-level (NOT console.error/warn) —
    // an expected degrade the boot smoke's console gate tolerates.
    const fallback = (reason) => {
      console.log("[shell] wasm streaming unavailable (" + reason + ") — using arrayBuffer instantiate");
      return fetchWasm()
        .then((resp) => resp.arrayBuffer())
        .then((bytes) => WebAssembly.instantiate(bytes, imports))
        .then(({ instance, module }) => successCallback(instance, module));
    };
    if (typeof WebAssembly.instantiateStreaming !== "function") {
      fallback("no instantiateStreaming").catch(onError);
    } else {
      WebAssembly.instantiateStreaming(streamable, imports)
        .then(({ instance, module }) => successCallback(instance, module))
        .catch((e) => fallback(e).catch(onError));
    }
    return {};
  };
}

let booted = false;
let rom = null;      // WEB-062: hoisted to module scope so boot() can drop the 12 MB
                     // closure copy after callMain (MEMFS then owns the only copy).
let _module = null;  // set once the engine module exists, for crash-path syncfs/audio cleanup
// WEB-002: the module-agnostic runtime registrations (persist timer, pagehide,
// visibility/audio-resume, crash net, Alt keydown, beforeunload, fullscreenchange)
// are armed exactly once. A re-boot after handleExit re-enters boot() and must not
// stack a second interval or a duplicate of each listener.
let _runtimeArmed = false;

// WEB-010: crash surface. Asyncify makes callMain() return at the first stack
// unwind (the rAF yield), so a mid-game wasm trap NEVER reaches boot().catch —
// it surfaces as a global window error/rejection instead. These handlers are the
// only net once the game is running: show a message and make one last-ditch
// attempt to flush saves so the user knows a reload preserves their auto-save.
function handleCrash() {
  // A clean engine exit (ROM rejection) is surfaced by handleExit, not here —
  // don't mislabel it a crash if the ExitStatus also trips the global handlers.
  if (_exitHandled) return;
  // Honest messaging: only promise an auto-save once at least one persist
  // actually succeeded (_savedOnce) — a crash before the first syncfs has no
  // save to come back to.
  showFatal(_savedOnce
    ? "The game crashed — reload to continue from your last auto-save."
    : "The game crashed — reload the page to try again.");
  try { _module?.FS?.syncfs(false, () => {}); } catch {}
}
let _savedOnce = false;

// WEB-029: hardened save persistence. emscripten's FS.syncfs is NOT reentrant —
// starting a second syncfs while one is in flight can corrupt the IDB commit — so
// an in-flight flag drops overlapping ticks. A failed persist (quota, IDB error)
// used to vanish into a `() => {}` callback while the page kept promising
// auto-save; now it raises a persistent banner, logs, and retries with capped
// exponential backoff. _savedOnce semantics are unchanged (set only on success).
let _syncInFlight = false;
let _persistRetryTimer = null;
let _persistBackoff = 0;
function showSaveBanner() { const b = $("save-banner"); if (b) b.hidden = false; }
function hideSaveBanner() { const b = $("save-banner"); if (b) b.hidden = true; }
function schedulePersistRetry() {
  if (_persistRetryTimer) return;                 // one retry armed at a time
  _persistBackoff = Math.min(_persistBackoff ? _persistBackoff * 2 : 2000, 30000);
  _persistRetryTimer = setTimeout(() => { _persistRetryTimer = null; persist(); }, _persistBackoff);
}
function persist() {
  // WEB-002: read the CURRENT module at call time — a re-boot after handleExit
  // swaps _module, and this single interval must follow it, not a captured ref.
  const mm = _module;
  if (!mm || _syncInFlight) return;
  _syncInFlight = true;
  try {
    mm.FS.syncfs(false, (e) => {
      _syncInFlight = false;
      if (e) {
        console.error("[shell] save persist failed:", e);
        showSaveBanner();
        schedulePersistRetry();
      } else {
        _savedOnce = true;
        _persistBackoff = 0;
        hideSaveBanner();
      }
    });
  } catch (e) {
    // Synchronous throw (FS torn down mid-call) — treat as a failed persist.
    _syncInFlight = false;
    console.error("[shell] save persist threw:", e);
    showSaveBanner();
    schedulePersistRetry();
  }
}

// WEB-035: iOS interruptions (call/Siri/alarm), some BT route changes, and tab
// backgrounding can suspend the WebAudio context; SDL kills its own resume timer
// after the first success, so without this the game goes permanently silent. Any
// user gesture or return-to-visible re-runs resume(). No-op until _module exists.
function resumeAudio() {
  const ctx = _module?.SDL2?.audioContext;
  if (ctx && ctx.state !== "running") ctx.resume().catch(() => {});
}

// WEB-031: two tabs on one origin share /save (IDBFS is last-write-wins per
// timer), so the second silently clobbers the first's progress. Take an
// exclusive Web Lock held for the whole session; a second tab can't acquire it
// and refuses to boot (recoverable — Play re-arms, not fatal). Feature-detected;
// if the API is missing or errors we don't block boot. _haveLock lets a WEB-009
// retry in THIS tab re-enter without deadlocking on the lock we already hold.
let _haveLock = false;
function acquireSingleInstanceLock() {
  return new Promise((resolve) => {
    if (_haveLock) { resolve(true); return; }
    if (!navigator.locks || !navigator.locks.request) { resolve(true); return; }
    try {
      navigator.locks.request("mgb64-game", { ifAvailable: true }, (lock) => {
        if (!lock) { resolve(false); return; }    // another tab holds it
        _haveLock = true;
        resolve(true);
        return new Promise(() => {});              // never resolves → hold for session lifetime
      }).catch(() => resolve(true));
    } catch { resolve(true); }
  });
}

// WEB-064: dismiss the in-game hint from JS so it disappears for everyone,
// including prefers-reduced-motion users (for whom the old CSS fade-out keyframe
// was disabled, stranding the pill on screen forever). The CSS opacity
// transition is cosmetic and auto-disabled under reduced motion — these timers
// are the single source of truth for when the hint goes away.
function armHintDismiss() {
  const hint = $("overlay-hint");
  if (!hint) return;
  setTimeout(() => hint.classList.add("hint-hidden"), 5400);
  setTimeout(() => { hint.hidden = true; }, 6000);
}

// WEB-002: keep the last engine stderr lines so a nonzero exit (the engine
// refusing a wrong-content/PAL/J ROM in rom_io.c) can be shown to the user
// instead of vanishing into devtools. Bounded ring — we only surface the tail.
let _stderrTail = [];
function recordStderr(text) {
  console.error(text);
  _stderrTail.push(String(text));
  if (_stderrTail.length > 20) _stderrTail.shift();
}

// WEB-002: the engine's main() returns nonzero when rom_io.c refuses the ROM
// (bad header, wrong game, or — after the US offsets are patched onto a PAL/J
// cart that passed the title scan — a decompression fault at frame 0). Under
// Asyncify callMain() unwinds at the first yield, so we learn the outcome via
// onExit, not a return value. A nonzero exit is a REJECTION, not a crash: re-show
// the gate with the reason + the last stderr lines + the Forget-stored-ROM
// shortcut, because the bad file is already in OPFS and would re-poison every
// future visit (the exact trap WEB-002 targets).
let _exitHandled = false;
// Final-review F1: suppresses the beforeunload confirm around reloads WE issue
// (the Forget-stored-ROM recovery flow) so recovery is never gated on a prompt.
let _intentionalReload = false;
function handleExit(code) {
  if (code === 0 || _exitHandled) return;
  _exitHandled = true;
  booted = false;
  hideFatal();
  // Silence the dead module's ScriptProcessorNode (mirrors the WEB-009 catch).
  try { _module?.SDL2?.audioContext?.close(); } catch {}
  const canvas = $("canvas"); if (canvas) canvas.hidden = true;
  const hint = $("overlay-hint"); if (hint) hint.hidden = true;
  $("gate").hidden = false;
  $("rom-ui").hidden = false;
  // The stored ROM is the one that was just refused — don't let a stale
  // "ROM ready" line contradict the rejection message beside it.
  $("rom-status").textContent = "Stored ROM was rejected — Forget it or pick a different file.";
  const tail = _stderrTail.filter(Boolean).slice(-6).join("\n");
  const msg = $("gate-msg");
  msg.style.whiteSpace = "pre-wrap";
  msg.textContent =
    "The engine refused this ROM (exit " + code + "). It's likely the wrong " +
    "game or region — GoldenEye 007 (USA) is required." +
    (tail ? "\n\n" + tail : "");
  // The bad ROM is stored in OPFS; make its removal one click away.
  const forget = $("forget"); if (forget) forget.hidden = false;
  const play = $("play"); if (play) play.disabled = false;
}

// WEB-002: instant client-side content check at pick time (no canonical ROM
// hash exists in-repo to SHA-1 against, so we mirror rom_io.c's own header+title
// gate on the picked bytes). Catches the common "12 MB non-GoldenEye file" case
// up front — the black-screen-then-repoison trap — rather than after a full boot.
// Returns null when the bytes look like a GoldenEye N64 ROM, else a reason string.
function validateRomContent(bytes) {
  if (!bytes || bytes.length < 0x40) return "That file is too small to be an N64 ROM.";
  // Normalize just the header to big-endian (.z64) for the checks, mirroring
  // rom_io.c's byteswap for .v64 (swap 2) / .n64 (swap 4).
  const h = bytes.slice(0, 0x40);
  if (h[0] === 0x37 && h[1] === 0x80) {
    for (let i = 0; i + 1 < h.length; i += 2) { const t = h[i]; h[i] = h[i + 1]; h[i + 1] = t; }
  } else if (h[0] === 0x40 && h[1] === 0x12) {
    for (let i = 0; i + 3 < h.length; i += 4) {
      const a = h[i], b = h[i + 1], c = h[i + 2], d = h[i + 3];
      h[i] = d; h[i + 1] = c; h[i + 2] = b; h[i + 3] = a;
    }
  }
  if (h[0] !== 0x80 || h[1] !== 0x37 || h[2] !== 0x12 || h[3] !== 0x40)
    return "That file isn't an N64 ROM (bad header). Expected a GoldenEye 007 .z64/.v64/.n64 dump.";
  // Internal cartridge title must contain "GOLDENEYE" (scan 0x20..0x34 as rom_io.c does).
  const GE = [0x47, 0x4F, 0x4C, 0x44, 0x45, 0x4E, 0x45, 0x59, 0x45];
  let isGE = false;
  for (let i = 0x20; i + 9 <= 0x34 && !isGE; i++) {
    isGE = true;
    for (let j = 0; j < 9; j++) { if (h[i + j] !== GE[j]) { isGE = false; break; } }
  }
  if (!isGE)
    return "That ROM isn't GoldenEye 007 (internal title mismatch). Please supply the GoldenEye 007 cartridge dump.";
  return null;
}

async function boot(romBytes) {
  if (booted) return;
  // WEB-062 guard: after a rejected ROM (handleExit) `rom` is nulled below, so a
  // stray re-Play would carry no bytes — send the user back to the picker cleanly
  // rather than throwing an opaque "Boot failed".
  if (!romBytes) {
    $("gate-msg").textContent = "Pick your ROM to continue (or Forget the stored one).";
    $("play").disabled = false;
    return;
  }
  // WEB-031: refuse a second concurrent instance BEFORE tearing down the gate, so
  // two tabs never last-write-wins clobber the shared /save. Recoverable, not
  // fatal: re-arm Play and leave the gate up.
  if (!(await acquireSingleInstanceLock())) {
    $("gate-msg").textContent =
      "MGB64 is already running in another tab. Close it (or reload this tab afterward) — two tabs share one browser save and would overwrite each other's progress.";
    $("play").disabled = false;
    return;
  }
  booted = true;
  // WEB-002: a prior ROM rejection latched _exitHandled (handleExit cleared
  // `booted` to let us return here). Clear the latch now that a fresh engine run
  // is starting, or this boot's own onExit — and a later mid-game handleCrash —
  // would be swallowed by the stale guard.
  _exitHandled = false;
  _stderrTail = [];  // fresh tail per run — a later rejection must not show a mix of two runs' stderr
  const canvas = $("canvas");
  const status = $("gate-msg");
  // WEB-039: keep the gate visible with staged progress until just before
  // callMain, so a healthy slow boot (fetch/compile/syncfs) is distinguishable
  // from the failure surfaces above instead of jumping straight to a black canvas.
  status.textContent = "Downloading engine…";
  // WEB-039: the wasm fetch + engine glue were kicked off before the gate; here we
  // just await the glue factory (the wasm itself streams in via instantiateWasm,
  // no separate buffered `wasmBinary` await). A script-load error still throws here
  // and lands in boot().catch (WEB-009), preserving the transient-failure reset.
  const createMGB64 = await loadEngineFactory();
  // WEB-039: instantiateWasm streams the wasm straight into compilation. Because
  // Emscripten never surfaces a rejected instantiateWasm, route any async
  // instantiation failure (a fetch blip, or a bad MIME whose fallback also fails)
  // into this reject so the `await` below sees it — the same place the old
  // `await wasmBinary` fetch error (WEB-033) landed, still driving WEB-009.
  let onInstantiateError;
  const instantiateFailed = new Promise((_, rej) => { onInstantiateError = rej; });
  // WEB-010: onAbort catches emscripten aborts (assertion/OOM) that don't throw
  // through JS; the window handlers (armed below) catch the RuntimeError traps.
  const m = await Promise.race([
    createMGB64({
      canvas, noInitialRun: true,
      // WEB-039: stream + compile the wasm instead of buffering it to wasmBinary.
      instantiateWasm: makeInstantiateWasm(onInstantiateError),
      onAbort: () => handleCrash(),
      // WEB-002: capture engine stderr and the process exit code so a ROM the
      // engine refuses surfaces to the user instead of a silent black canvas.
      printErr: (t) => recordStderr(t),
      onExit: (code) => handleExit(code),
    }),
    instantiateFailed,   // only ever rejects — routes an instantiateWasm failure here
  ]);
  _module = m;
  status.textContent = "Preparing saves…";
  m.FS.mkdir("/rom"); m.FS.writeFile("/rom/baserom.z64", romBytes);
  m.FS.mkdir("/save"); m.FS.mount(m.IDBFS, {}, "/save");
  await new Promise((res, rej) => m.FS.syncfs(true, (e) => e ? rej(e) : res()));
  // WEB-002: these runtime registrations are module-agnostic (they route through
  // _module at call time, never a captured instance), so arm them exactly ONCE.
  // A re-boot after handleExit re-enters boot() but must not stack a second
  // interval or duplicate each listener. The crash-net arming point is unchanged
  // — still after syncfs and before callMain — just made idempotent.
  if (!_runtimeArmed) {
    _runtimeArmed = true;
    // WEB-029: guarded, self-healing persist on a timer + pagehide (see persist()).
    setInterval(persist, 5000);
    addEventListener("pagehide", persist);
    // WEB-030: also flush when the tab is backgrounded — pagehide alone can miss a
    // mobile process-kill, losing up to ~5 s (the mission-complete EEPROM window).
    // WEB-035: and nudge a suspended AudioContext back when we return to visible.
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "hidden") persist();
      else resumeAudio();
    });
    // WEB-019 + WEB-035: capture-phase keydown. Alt is the N64 L trigger (stubs.c),
    // but Alt+D focuses the address bar and a bare Alt opens Firefox's menu —
    // either steals focus and drops every subsequent key mid-firefight.
    // preventDefault kills the browser action; SDL still receives the key. Meta
    // (Cmd) combos are left alone. Any keypress also counts as an audio-resume
    // gesture.
    // AltGr limitation: on Win/Linux international layouts AltGr sets altKey, so
    // the L trigger can fire spuriously — accepted; pointer-locked gameplay has no
    // text entry, so there is nothing for AltGr to compose here.
    addEventListener("keydown", (e) => {
      if (e.altKey && !e.metaKey) e.preventDefault();
      resumeAudio();
    }, true);
    // WEB-035: a pointer press is a user gesture too (covers the Safari
    // gesture-window case where the audio context is created after the click).
    addEventListener("pointerdown", resumeAudio);
    // WEB-010: arm the global crash net only now that the engine is actually
    // running, so it can't fire on a boot-phase error already handled by
    // boot().catch.
    addEventListener("error", handleCrash);
    addEventListener("unhandledrejection", handleCrash);
    // WEB-008: Ctrl+W (crouch-forward, the most common FPS chord) and Cmd+W close
    // the tab, and the browser won't let preventDefault stop it — the only lever
    // is a beforeunload confirm. Arm it once booted so a mis-hit chord asks before
    // discarding mission progress. (No custom string: modern browsers show their
    // own generic prompt, but any non-empty returnValue triggers it.)
    // Final-review F1: never confirm on our own programmatic reload (Forget
    // flow) or while sitting at the gate after an engine rejection — the
    // prompt would gate the recovery path the WEB-002 feature itself built.
    addEventListener("beforeunload", (e) => {
      if (_intentionalReload || _exitHandled) return;
      e.preventDefault(); e.returnValue = "";
    });
    // WEB-008: while fullscreen, best-effort Keyboard Lock so Ctrl+W / Cmd+W and
    // other system chords route to the game instead of the browser. Silently a
    // no-op where unsupported (Safari/Firefox) or not fullscreen — never throws.
    const applyKeyboardLock = () => {
      try {
        if (document.fullscreenElement && navigator.keyboard && navigator.keyboard.lock) {
          navigator.keyboard.lock(["KeyW", "KeyA", "KeyS", "KeyD",
                                   "ControlLeft", "ControlRight", "Escape"]).catch(() => {});
        } else if (navigator.keyboard && navigator.keyboard.unlock) {
          navigator.keyboard.unlock();
        }
      } catch {}
    };
    document.addEventListener("fullscreenchange", applyKeyboardLock);
  }
  // WEB-039: reveal the game view only now — just before callMain.
  status.textContent = "Starting…";
  $("gate").hidden = true;
  canvas.hidden = false;
  // WEB-040: aim is right-click; without this the browser context menu pops on
  // every RMB whenever the pointer isn't locked.
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  // Additive UI only: reveal the unobtrusive in-game hint (self-dismisses via
  // armHintDismiss — WEB-064). Purely cosmetic; no bearing on the boot contract.
  const hint = $("overlay-hint");
  if (hint) { hint.hidden = false; armHintDismiss(); }
  const args = ["--rom", "/rom/baserom.z64", "--savedir", "/save"];
  const unlockAll = $("unlock-all");
  if (unlockAll && unlockAll.checked) args.push("--unlock-all-levels");
  // WEB-018: apply the shell input prefs via the engine's real CLI override
  // mechanism (main_pc.c --config-override Section.Key=value; keys registered in
  // platform_sdl.c). Passed ALWAYS, not only when non-default: the default slider
  // (1.00× → Input.MouseSensitivity=0.15) and unchecked box (Input.InvertY=0) are
  // the engine's own defaults, so a default boot stays byte-identical to before
  // this feature, while unconditional passing keeps "what the control shows" ==
  // "what the engine runs" with no default-omit branch to drift out of sync.
  const sensEl = $("mouse-sens");
  const mult = sensEl ? Number(sensEl.value) : 1;
  const sens = MOUSE_SENS_DEFAULT * (Number.isFinite(mult) && mult > 0 ? mult : 1);
  args.push("--config-override", "Input.MouseSensitivity=" + sens.toFixed(4));
  const invEl = $("invert-y");
  args.push("--config-override", "Input.InvertY=" + (invEl && invEl.checked ? "1" : "0"));
  // Faithful HUD defaults for the web demo (owner ruling 2026-07-17): the N64
  // aim reticle only appears while aiming — no modern hip-fire crosshair, no
  // hit-marker flashes, no on-target tint — and there is no radar/minimap.
  // These four are the HUD entries of the engine's --faithful preset
  // (platform_sdl.c s_faithfulPreset); the rest of that preset (post-FX, FOV)
  // deliberately stays at remaster defaults on web. RenderScale is the exception:
  // PERF-030 defaults it to native res (1.0) on web — 2x SSAA over CSS x DPR is a
  // ~5x pixel load the compositor would just downscale.
  args.push("--config-override", "Input.ModernCrosshair=0");
  args.push("--config-override", "Input.HitMarkers=0");
  args.push("--config-override", "Input.ReticleTargetFeedback=0");
  args.push("--config-override", "Input.MinimapEnabled=0");
  // W4.1: pass every ?arg=<value> from the URL through to callMain, appended
  // AFTER the defaults above so a query arg can override them (main_pc.c takes
  // the last --config-override / later flag). This is a DEV/TEST surface: the
  // browser regression lane (tools/web/web_frame_probe.sh + webcap.mjs, and the
  // web input-tape harness) drives a level directly via
  // "?arg=--level&arg=dam&arg=--deterministic". No gating is needed because it
  // is client-side only — this is a BYO-ROM app with no server, so a user can
  // only ever affect their OWN in-tab session (exactly the same trust domain as
  // typing into the devtools console). It grants no capability the page owner
  // doesn't already have over their own browser.
  try {
    for (const a of new URLSearchParams(location.search).getAll("arg")) args.push(a);
  } catch {}
  m.callMain(args);
  // WEB-062: MEMFS now holds its own /rom copy, so drop the 12 MB byte array the
  // JS side was holding (both the local param and the module-scope reference) —
  // frees a full ROM's worth of heap under mobile eviction pressure. Do NOT
  // unlink the MEMFS copy; the engine reads it via --rom for the whole session.
  romBytes = null;
  rom = null;
}

(async () => {
  // WEB-039: start pulling the engine down NOW — BEFORE the async WebGPU gate — so
  // the wasm download overlaps requestAdapter() (the two were serialized before,
  // the fetch waiting on the adapter). instantiateWasm streams from this shared
  // Response promise at boot; the glue script prefetches alongside it. Pre-attach
  // benign catches so an early network failure doesn't log as an "unhandled
  // rejection" before boot() consumes it — instantiateWasm / boot() still see and
  // surface the real error (WEB-033 → boot().catch → WEB-009 reset).
  loadEngineFactory().catch(() => {});   // memoized; boot() reuses _enginePromise
  wasmPromise = fetchWasm();
  wasmPromise.catch(() => {});
  const err = await gate();
  if (err) { $("gate-msg").textContent = err; return; }
  // WEB-011: iOS/iPadOS Safari passes the WebGPU gate but the engine is
  // keyboard/mouse/gamepad only (no pointer lock on iOS; touch = fire-only), so
  // a phone user would invest a 12 MB upload before discovering they can't play.
  // Warn a coarse-pointer / touch-primary device up front — but still allow boot
  // (paired keyboards and controllers do exist). Not a gate refusal.
  const touchPrimary =
    (matchMedia && matchMedia("(pointer: coarse)").matches) &&
    (navigator.maxTouchPoints || 0) > 0;
  $("gate-msg").textContent = touchPrimary
    ? "Heads up: this game needs a keyboard & mouse (or a gamepad). On a touch-only device you can look around but not move — pair a keyboard/controller for full control."
    : "";
  $("rom-ui").hidden = false;
  // WEB-066: restore + persist the unlock-all preference across visits.
  const unlockAll = $("unlock-all");
  if (unlockAll) {
    try { unlockAll.checked = localStorage.getItem("mgb64-unlock-all") === "1"; } catch {}
    unlockAll.addEventListener("change", () => {
      try { localStorage.setItem("mgb64-unlock-all", unlockAll.checked ? "1" : "0"); } catch {}
    });
  }
  // WEB-018: restore + persist the input prefs across visits (mirrors unlock-all).
  // The <input type="range"> clamps a stale/out-of-bounds stored value to its
  // min/max on assignment, and the engine re-clamps the derived override too, so
  // a corrupt localStorage entry can never push sensitivity out of range.
  const mouseSens = $("mouse-sens");
  const mouseSensVal = $("mouse-sens-val");
  if (mouseSens) {
    const showSens = () => {
      if (mouseSensVal) mouseSensVal.textContent = Number(mouseSens.value).toFixed(2) + "×";
    };
    try {
      const saved = localStorage.getItem("mgb64-mouse-sens");
      if (saved !== null) mouseSens.value = saved;
    } catch {}
    showSens();
    mouseSens.addEventListener("input", () => {
      showSens();
      try { localStorage.setItem("mgb64-mouse-sens", mouseSens.value); } catch {}
    });
  }
  const invertY = $("invert-y");
  if (invertY) {
    try { invertY.checked = localStorage.getItem("mgb64-invert-y") === "1"; } catch {}
    invertY.addEventListener("change", () => {
      try { localStorage.setItem("mgb64-invert-y", invertY.checked ? "1" : "0"); } catch {}
    });
  }
  rom = await loadStoredRom();
  const showReady = () => { $("rom-status").textContent = "ROM ready (stored in this browser)."; $("play").disabled = false; $("forget").hidden = false; };
  if (rom) showReady();
  $("rom-input").addEventListener("change", async (ev) => {
    const f = ev.target.files[0]; if (!f) return;
    // WEB-063: a rejected pick must (a) not silently discard the ROM already
    // loaded — say it's still active — and (b) clear the input so re-picking the
    // SAME file re-fires 'change' (browsers suppress it for an identical value).
    const kept = () => rom ? " Keeping the previously loaded ROM." : "";
    if (f.size !== ROM_SIZE) {
      $("rom-status").textContent = `Wrong size (${f.size} bytes) — expected exactly 12 MB.` + kept();
      ev.target.value = "";
      return;
    }
    const picked = new Uint8Array(await f.arrayBuffer());
    // WEB-002: reject wrong-content files at pick time so they never reach OPFS
    // (an unrecognized 12 MB file, stored, would re-poison every future visit).
    const bad = validateRomContent(picked);
    if (bad) { $("rom-status").textContent = bad + kept(); ev.target.value = ""; return; }
    rom = picked;
    const stored = await storeRom(rom);
    if (stored) {
      showReady();
    } else {
      // Persistence failed (e.g. OPFS quota) — booting from the in-memory
      // bytes still works, so still enable Play.
      $("rom-status").textContent = "Couldn't store the ROM in browser storage (quota?). You can still pick it again next visit.";
      $("play").disabled = false;
    }
  });
  $("forget").addEventListener("click", async () => { _intentionalReload = true; await forgetRom(); location.reload(); });
  // Boot on click — the user gesture unlocks WebAudio.
  $("play").addEventListener("click", () => {
    $("play").disabled = true;
    boot(rom).catch(e => {
      // WEB-009: a transient boot failure (wasm fetch blip, syncfs rejection)
      // must not wedge the page. Reset all boot state so Play works again and
      // the gate returns, and close the AudioContext SDL opened during factory
      // init (a ScriptProcessorNode keeps burning CPU otherwise).
      booted = false;
      // WEB-039 preservation: the preloaded wasm promise may itself be the
      // rejected one (a fetch blip). Drop it so the next Play re-fetches from
      // scratch, keeping WEB-009's transient-retry semantics (the original boot()
      // re-fetched on every call).
      wasmPromise = null;
      // WEB-009: mirror of the wasmPromise reset — a rejected engine load must not
      // stay memoized. A transient ge007_web.js script-load blip would otherwise
      // pin _enginePromise to that rejection and every Play retry would reject.
      _enginePromise = null;
      // If the failure surfaced as a fatal panel (onAbort/bring-up), drop it —
      // otherwise it sits at z-index 9999 over the re-shown gate and the Play
      // retry is unreachable; also re-arms the one-shot latch for the retry.
      hideFatal();
      $("canvas").hidden = true;
      const hint = $("overlay-hint"); if (hint) hint.hidden = true;
      try { _module?.SDL2?.audioContext?.close(); } catch {}
      $("gate").hidden = false;
      $("gate-msg").textContent = "Boot failed: " + e;
      $("play").disabled = false;
    });
  });
})();
