// web_boot_smoke.mjs — headless-Chrome boot smoke for the MGB64 browser build.
//
// Drives a real system Chrome against a locally-served copy of dist/web, injects
// a ROM into the file picker (no upload — CDP DOM.setFileInputFiles reads a local
// path straight into the page), clicks Play, and asserts the WebGPU engine boots
// to a live, rendering state. This is the browser build's regression net (the web
// surface changed ~20x in one day) and the validation harness for the upcoming
// Asyncify experiment.
//
// Zero npm dependencies by design: the CDP client speaks the DevTools protocol
// over Chrome's --remote-debugging-pipe (fd 3 read-by-Chrome / fd 4 written-by-
// Chrome, NUL-delimited JSON) — no WebSocket (Node 20 has no global WS) and no
// puppeteer. The static file server and PNG mean-luminance decoder both use only
// Node built-ins (http, zlib).
//
// Contract asserted (mirrors web/mgb64-shell.js + web/index.html):
//   1. capability gate passes  -> #rom-ui unhidden, #gate-msg not a refusal
//   2. ROM accepted            -> #rom-status == "ROM ready (stored in this browser)."
//   3. Play -> boot            -> #gate hidden, #canvas visible AND STAYS visible
//   4. engine reaches running  -> >= N rAF frames elapsed, no #fatal, no "Boot failed"
//   5. zero unexpected console errors / pageerrors (benign lines whitelisted below)
//   6. (stretch) post-boot screenshot is non-black (catches an inert backend)
//
// Exit: 0 = PASS, 1 = FAIL. (SKIP for missing deps is the wrapper's job, exit 125.)

import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { readFile, mkdtemp, rm } from "node:fs/promises";
import { existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, extname } from "node:path";
import zlib from "node:zlib";

// ---- args -----------------------------------------------------------------
function argVal(name, def) {
  const i = process.argv.indexOf(name);
  return i !== -1 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const DIST = argVal("--dist");
const ROM = argVal("--rom");
const CHROME = argVal("--chrome");
const FRAMES = Number(argVal("--frames", "30"));           // rAF frames = "running"
const BUDGET_MS = Number(argVal("--budget", process.env.MGB64_WEB_SMOKE_BUDGET || "45")) * 1000;
const DO_SHOT = process.argv.indexOf("--no-screenshot") === -1;
const SHOT_MIN_MEAN = Number(argVal("--shot-min-mean", "2.0"));

if (!DIST || !ROM || !CHROME) {
  console.error("usage: web_boot_smoke.mjs --dist <dir> --rom <path> --chrome <bin> [--frames N] [--budget S]");
  process.exit(2);
}

// Console lines that are benign on a HEALTHY headless boot. Anything NOT matching
// here (and every pageerror / uncaught exception) fails the gate. Kept tight and
// empirically grounded in an observed clean run — a new line means the web surface
// changed and this list should be revisited (that is the regression net working).
//
// NOTE on WebGPU: gfx_webgpu.c writes both success and FAILURE diagnostics to
// stderr with a "[webgpu] " prefix (emscripten routes stderr -> console.error), so
// this deliberately whitelists ONLY the specific success/info lines. The failure
// lines ("... inert", "... failed", "device lost/error", "handoff incomplete") are
// NOT listed and correctly fail the gate — that is how an inert-backend regression
// is caught even if the canvas stays up.
const CONSOLE_WHITELIST = [
  // SDL2's WebAudio output uses a ScriptProcessorNode; the deprecation notice is
  // benign and unavoidable until SDL migrates to AudioWorklet.
  /ScriptProcessorNode is deprecated/i,
  // Healthy WebGPU backend bring-up signature (src/platform/fast3d/gfx_webgpu.c).
  /^\[webgpu\] adapter backend=/i,
  /^\[webgpu\] maxTextureDimension2D=/i,
  /^\[webgpu\] backend initialized \(surface format=/i,
  /^\[webgpu\] adopted host device\/surface \(format=/i,
];
const isWhitelisted = (t) => CONSOLE_WHITELIST.some((re) => re.test(t || ""));

const log = (...a) => console.log("[web-smoke]", ...a);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- static file server (dist/web on an ephemeral localhost port) ---------
const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json",
};
function startServer(root) {
  const server = createServer(async (req, res) => {
    try {
      let p = decodeURIComponent(new URL(req.url, "http://x").pathname);
      if (p === "/") p = "/index.html";
      // strip ?v=HASH etc. — path only. Confine to root (no traversal).
      const full = join(root, p);
      if (!full.startsWith(root) || !existsSync(full)) { res.writeHead(404); res.end("not found"); return; }
      const body = await readFile(full);
      res.writeHead(200, { "content-type": MIME[extname(full)] || "application/octet-stream" });
      res.end(body);
    } catch (e) { res.writeHead(500); res.end(String(e)); }
  });
  return new Promise((resolve) => server.listen(0, "127.0.0.1", () => resolve({ server, port: server.address().port })));
}

// ---- minimal CDP-over-pipe client -----------------------------------------
class CDP {
  constructor(child) {
    this.child = child;
    this.wr = child.stdio[3];    // Chrome reads its input from fd 3
    this.rd = child.stdio[4];    // Chrome writes its output to fd 4
    this.id = 0;
    this.pending = new Map();
    this.listeners = [];         // (method, params, sessionId) => void
    this.buf = Buffer.alloc(0);
    this.rd.on("data", (c) => this._onData(c));
  }
  _onData(chunk) {
    this.buf = Buffer.concat([this.buf, chunk]);
    let i;
    while ((i = this.buf.indexOf(0)) !== -1) {
      const txt = this.buf.subarray(0, i).toString("utf8");
      this.buf = this.buf.subarray(i + 1);
      let m; try { m = JSON.parse(txt); } catch { continue; }
      if (m.id && this.pending.has(m.id)) {
        const { resolve, reject } = this.pending.get(m.id);
        this.pending.delete(m.id);
        m.error ? reject(new Error(m.method + ": " + JSON.stringify(m.error))) : resolve(m.result);
      } else if (m.method) {
        for (const fn of this.listeners) fn(m.method, m.params, m.sessionId);
      }
    }
  }
  on(fn) { this.listeners.push(fn); }
  send(method, params = {}, sessionId) {
    const id = ++this.id;
    const payload = { id, method, params, ...(sessionId ? { sessionId } : {}) };
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.wr.write(JSON.stringify(payload) + "\0");
    });
  }
}

// ---- tiny PNG -> mean luminance (built-in zlib; catches inert/black backend) --
function pngMeanLuma(png) {
  // PNG: 8-byte sig, then chunks [len(4) type(4) data crc(4)]. We need IHDR + IDATs.
  if (png.readUInt32BE(0) !== 0x89504e47) throw new Error("not a PNG");
  let off = 8, width = 0, height = 0, bitDepth = 0, colorType = 0;
  const idat = [];
  while (off < png.length) {
    const len = png.readUInt32BE(off);
    const type = png.toString("ascii", off + 4, off + 8);
    const data = png.subarray(off + 8, off + 8 + len);
    if (type === "IHDR") {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4);
      bitDepth = data[8]; colorType = data[9];
    } else if (type === "IDAT") idat.push(data);
    else if (type === "IEND") break;
    off += 12 + len;
  }
  if (bitDepth !== 8) throw new Error("unsupported bitDepth " + bitDepth);
  const channels = { 0: 1, 2: 3, 4: 2, 6: 4 }[colorType];
  if (!channels) throw new Error("unsupported colorType " + colorType);
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(height * stride);
  // Un-filter scanlines (PNG filter types 0..4) in place.
  const bpp = channels;
  for (let y = 0; y < height; y++) {
    const ft = raw[y * (stride + 1)];
    const src = raw.subarray(y * (stride + 1) + 1, y * (stride + 1) + 1 + stride);
    const cur = out.subarray(y * stride, y * stride + stride);
    const prior = y > 0 ? out.subarray((y - 1) * stride, (y - 1) * stride + stride) : null;
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? cur[x - bpp] : 0;
      const b = prior ? prior[x] : 0;
      const c = prior && x >= bpp ? prior[x - bpp] : 0;
      let v = src[x];
      if (ft === 1) v += a;
      else if (ft === 2) v += b;
      else if (ft === 3) v += (a + b) >> 1;
      else if (ft === 4) {
        const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      cur[x] = v & 0xff;
    }
  }
  // Mean luminance over RGB (ignore alpha). Grayscale => single channel.
  let sum = 0, n = 0;
  for (let i = 0; i < out.length; i += channels) {
    if (channels >= 3) sum += 0.299 * out[i] + 0.587 * out[i + 1] + 0.114 * out[i + 2];
    else sum += out[i];
    n++;
  }
  return sum / n;
}

// ---- main -----------------------------------------------------------------
let child, udd, srv, failed = false, skipped = false, skipReason = "";
const consoleErrors = [];  // collected {kind,text}

async function cleanup() {
  try { child && child.kill("SIGKILL"); } catch {}
  try { srv && srv.server.close(); } catch {}
  try { udd && await rm(udd, { recursive: true, force: true }); } catch {}
}
/* Review fix: an interrupted run (Ctrl-C during a manual/debug session) must
 * not leave the Chrome user-data-dir behind — it holds the OPFS copy of the
 * ROM, and this repo's contamination discipline says ROM bytes never persist
 * outside the gitignored originals. Chrome itself always dies with the pipe;
 * this covers the temp dir. */
for (const s of ["SIGINT", "SIGTERM"]) {
  process.on(s, () => { cleanup().then(() => process.exit(130)); });
}
function fail(msg) { failed = true; console.error("[web-smoke] FAIL:", msg); }
// SKIP (exit 125): the *environment* can't run the test (e.g. a headless/GPU-less
// runner where WebGPU has no adapter) — distinct from a real regression. Not a
// broken shell: NEG-control gate breakage leaves gate-msg at "Checking your
// browser…", which is NOT a capability refusal and still FAILs.
function skip(msg) { skipped = true; skipReason = msg; console.error("[web-smoke] SKIP:", msg); }
const CAP_REFUSAL = /needs WebGPU|Origin-Private File System|no compatible GPU|adapter request failed|can't render here/i;

async function evalOn(cdp, sess, expression, awaitPromise = false) {
  const r = await cdp.send("Runtime.evaluate", { expression, awaitPromise, returnByValue: true }, sess);
  if (r.exceptionDetails) throw new Error("eval threw: " + (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
  return r.result.value;
}

// Poll a JS boolean expression until true or timeout; returns true/false.
async function waitFor(cdp, sess, expr, timeoutMs, everyMs = 250) {
  const t0 = Date.now();
  for (;;) {
    let v = false;
    try { v = await evalOn(cdp, sess, `!!(${expr})`); } catch {}
    if (v) return true;
    if (Date.now() - t0 > timeoutMs) return false;
    await sleep(everyMs);
  }
}

async function run() {
  srv = await startServer(DIST);
  const URL0 = `http://127.0.0.1:${srv.port}/`;
  log("serving", DIST, "at", URL0);

  udd = await mkdtemp(join(tmpdir(), "mgb64-websmoke-"));
  const args = [
    "--headless=new", `--user-data-dir=${udd}`,
    "--no-first-run", "--no-default-browser-check", "--disable-extensions",
    "--remote-debugging-pipe",
    "--enable-unsafe-webgpu",                 // belt-and-suspenders; localhost already exposes WebGPU
    "--mute-audio", "--window-size=1280,960",
    "--disable-background-timer-throttling",  // keep rAF ticking while "backgrounded"
    "--disable-renderer-backgrounding",
    "--disable-backgrounding-occluded-windows",
    "about:blank",
  ];
  log("launching Chrome:", CHROME);
  child = spawn(CHROME, args, { stdio: ["ignore", "ignore", "ignore", "pipe", "pipe"] });
  child.on("exit", (code, sig) => { if (code && code !== 0 && !failed) log("chrome exited", code, sig); });
  const cdp = new CDP(child);

  // Overall watchdog — never exceed the runtime budget.
  const watchdog = setTimeout(() => { fail(`budget of ${BUDGET_MS}ms exceeded`); cleanup().then(() => process.exit(1)); }, BUDGET_MS + 15000);

  // Collect console errors / uncaught exceptions across the session.
  cdp.on((method, params) => {
    if (method === "Runtime.exceptionThrown") {
      const d = params.exceptionDetails;
      const text = d.exception?.description || d.text || "exception";
      consoleErrors.push({ kind: "pageerror", text });
    } else if (method === "Runtime.consoleAPICalled" && (params.type === "error" || params.type === "warning")) {
      const text = (params.args || []).map((a) => a.value ?? a.description ?? a.unserializableValue ?? "").join(" ");
      consoleErrors.push({ kind: "console." + params.type, text });
    } else if (method === "Log.entryAdded" && (params.entry.level === "error" || params.entry.level === "warning")) {
      consoleErrors.push({ kind: "log." + params.entry.level, text: params.entry.text });
    }
  });

  // Attach flatly so every command carries a sessionId.
  const { targetId } = await cdp.send("Target.createTarget", { url: "about:blank" });
  const { sessionId: sess } = await cdp.send("Target.attachToTarget", { targetId, flatten: true });
  await cdp.send("Page.enable", {}, sess);
  await cdp.send("Runtime.enable", {}, sess);
  await cdp.send("Log.enable", {}, sess);
  await cdp.send("DOM.enable", {}, sess);

  // Frame counter installed BEFORE any page script runs, so the engine's rAF loop
  // is counted regardless of when emscripten binds requestAnimationFrame.
  await cdp.send("Page.addScriptToEvaluateOnNewDocument", {
    source: `(() => { window.__mgb64_raf = 0;
      const orig = window.requestAnimationFrame.bind(window);
      window.requestAnimationFrame = (cb) => orig((t) => { window.__mgb64_raf++; return cb(t); });
    })();`,
  }, sess);

  log("navigating…");
  await cdp.send("Page.navigate", { url: URL0 }, sess);
  await waitFor(cdp, sess, "document.readyState === 'complete'", 15000);

  // 1) capability gate: #rom-ui revealed only after gate() resolves null.
  const gated = await waitFor(cdp, sess, "!document.getElementById('rom-ui').hidden", 15000);
  const gateMsg = await evalOn(cdp, sess, "document.getElementById('gate-msg').textContent");
  if (!gated) {
    if (CAP_REFUSAL.test(gateMsg)) return skip("environment lacks a usable WebGPU/OPFS capability: " + gateMsg);
    fail("capability gate never passed (rom-ui stayed hidden). gate-msg: " + JSON.stringify(gateMsg));
    return;
  }
  log("gate passed. gate-msg:", JSON.stringify(gateMsg));

  // 2) ROM injection via CDP (local path read straight into the picker; no upload).
  const doc = await cdp.send("DOM.getDocument", { depth: 1 }, sess);
  const { nodeId } = await cdp.send("DOM.querySelector", { nodeId: doc.root.nodeId, selector: "#rom-input" }, sess);
  if (!nodeId) { fail("#rom-input not found"); return; }
  await cdp.send("DOM.setFileInputFiles", { files: [ROM], nodeId }, sess);
  log("ROM injected:", ROM);

  const ready = await waitFor(cdp, sess,
    "document.getElementById('rom-status').textContent === 'ROM ready (stored in this browser).' && !document.getElementById('play').disabled",
    15000);
  const romStatus = await evalOn(cdp, sess, "document.getElementById('rom-status').textContent");
  if (!ready) { fail("ROM was not accepted. rom-status: " + JSON.stringify(romStatus)); return; }
  log("ROM accepted. rom-status:", JSON.stringify(romStatus));

  // 3) Play -> boot. userGesture:true satisfies WebAudio/gesture-gated paths.
  await cdp.send("Runtime.evaluate", { expression: "document.getElementById('play').click()", userGesture: true }, sess);
  log("clicked Play; booting (budget " + (BUDGET_MS / 1000) + "s)…");

  // 4) reach a running state: canvas visible, gate hidden, no fatal, no Boot-failed,
  //    and >= FRAMES rAF frames elapsed. Classify each poll so a hard boot failure
  //    (fatal panel / "Boot failed") aborts immediately instead of waiting the full
  //    budget — negative controls fail fast, real regressions surface quickly.
  const classify = `(() => {
    const g = document.getElementById('gate-msg').textContent;
    const fatal = document.getElementById('fatal');
    if (fatal && !fatal.hidden) return 'fatal:' + fatal.textContent;
    if (/^Boot failed/.test(g)) return 'bootfail:' + g;
    const canvas = document.getElementById('canvas');
    if (!canvas.hidden && document.getElementById('gate').hidden && window.__mgb64_raf >= ${FRAMES}) return 'running';
    return 'pending:raf=' + window.__mgb64_raf + ',canvasHidden=' + canvas.hidden;
  })()`;
  const t0 = Date.now();
  let cls = "pending";
  for (;;) {
    try { cls = await evalOn(cdp, sess, classify); } catch (e) { cls = "eval-error:" + e.message; }
    if (cls === "running") break;
    if (cls.startsWith("fatal:")) { fail("fatal panel during boot: " + cls.slice(6)); return; }
    if (cls.startsWith("bootfail:")) { fail("engine boot failed: " + cls.slice(9)); return; }
    if (Date.now() - t0 > BUDGET_MS) { fail("engine never reached running state within budget. last: " + cls); return; }
    await sleep(300);
  }
  log("running (" + ((Date.now() - t0) / 1000).toFixed(1) + "s):",
    await evalOn(cdp, sess, "'raf=' + window.__mgb64_raf"));

  // "STAYS visible": settle ~3s, then confirm canvas still up, no fatal appeared,
  // and rAF kept advancing (the engine is still driving frames, not frozen).
  const rafBefore = await evalOn(cdp, sess, "window.__mgb64_raf");
  await sleep(3000);
  const post = JSON.parse(await evalOn(cdp, sess, `JSON.stringify({
    raf: window.__mgb64_raf,
    canvasHidden: document.getElementById('canvas').hidden,
    fatal: !!document.getElementById('fatal') && document.getElementById('fatal').textContent,
    gateMsg: document.getElementById('gate-msg').textContent,
  })`));
  if (post.canvasHidden) { fail("canvas went hidden after boot (regressed): " + JSON.stringify(post)); return; }
  if (post.fatal) { fail("fatal panel appeared after boot: " + post.fatal); return; }
  if (/^Boot failed/.test(post.gateMsg)) { fail("gate-msg became 'Boot failed' after boot: " + post.gateMsg); return; }
  if (post.raf <= rafBefore) { fail(`rAF loop stalled after boot (frames ${rafBefore} -> ${post.raf})`); return; }
  log(`stayed live: canvas visible, no fatal, frames advanced ${rafBefore} -> ${post.raf}`);

  // 6) stretch: screenshot must be non-black (the FPS overlay alone guarantees
  //    some luminance once the backend is actually presenting frames).
  if (DO_SHOT) {
    let best = -1;
    for (let i = 0; i < 3; i++) {
      const { data } = await cdp.send("Page.captureScreenshot", { format: "png" }, sess);
      try { best = Math.max(best, pngMeanLuma(Buffer.from(data, "base64"))); } catch (e) { log("screenshot decode:", e.message); }
      await sleep(600);
    }
    log("screenshot max mean-luma:", best.toFixed(3), "(threshold >", SHOT_MIN_MEAN + ")");
    if (best < SHOT_MIN_MEAN) { fail(`post-boot screenshot is ~black (mean ${best.toFixed(3)} < ${SHOT_MIN_MEAN}) — inert backend?`); return; }
  }

  // 5) console-error gate. Print everything seen; fail on any non-whitelisted line.
  const bad = consoleErrors.filter((e) => e.kind === "pageerror" || !isWhitelisted(e.text));
  if (consoleErrors.length) {
    log(`console diagnostics (${consoleErrors.length} total):`);
    for (const e of consoleErrors) console.log("   ", e.kind, "|", e.text);
  } else log("console clean (no errors/warnings)");
  if (bad.length) { fail(`${bad.length} unexpected console error(s)/exception(s) — see above`); return; }

  clearTimeout(watchdog);
}

try {
  await run();
} catch (e) {
  fail("uncaught: " + (e && e.stack || e));
}
await cleanup();
if (skipped && !failed) { console.error("[web-smoke] RESULT: SKIP (" + skipReason + ")"); process.exit(125); }
if (failed) { console.error("[web-smoke] RESULT: FAIL"); process.exit(1); }
console.log("[web-smoke] RESULT: PASS");
process.exit(0);
