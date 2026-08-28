// webcap.mjs — drive the MGB64 browser build in headless Chrome, reach REAL
// gameplay in a level, and (optionally) assert FRAME COMPLETENESS.
//
// This is the productionized descendant of the PERF-005b flood-hunt prototype.
// Its reason to exist beyond web_boot_smoke.mjs: boot-smoke only proves the
// backend presents SOMETHING non-black. It cannot see an INCOMPLETE frame — the
// PERF-005 "sky-flood" bleed (a single colour flooding the viewport under CPU
// load) shipped precisely because a non-black check passes a flood frame. This
// harness adds the flood-frame detector (--assert-complete) and the load /
// input surfaces (--throttle, --keys, --burst) needed to *provoke* the bleed.
//
// Zero npm dependencies by design (mirrors web_boot_smoke.mjs): the CDP client
// speaks DevTools over Chrome's --remote-debugging-pipe (fd 3 read-by-Chrome /
// fd 4 written-by-Chrome, NUL-delimited JSON) — no WebSocket, no puppeteer. The
// static file server and the PNG decoder use only Node built-ins (http, zlib).
//
// This is a TOOL, not a ctest: it does NOT implement SKIP semantics (its wrapper
// web_frame_probe.sh gates on missing deps and exits 125). It also does not fail
// on console noise by default — the wrapper composes passes; this stays a sharp
// single-purpose instrument. Exit: 0 = OK, 1 = FAIL/flood, 2 = usage.
//
// usage:
//   node webcap.mjs --dist <dir> --rom <path> --chrome <bin> [--out <png>]
//     [--query "arg=--level&arg=dam"]   URL-encoded ?arg= passthrough (shell W4.1)
//     [--settle-ms 8000]                wait after the engine reaches running
//     [--window 1280,800]               Chrome window size (WxH or W,H)
//     [--throttle N]                    CDP CPU throttle (rate N; PERF-005 needs it)
//     [--keys "<spec>"]                 scripted key input (grammar below)
//     [--burst N:M]                     capture N screenshots every M ms
//     [--assert-complete T]             FAIL if any one colour > T% of pixels
//     [--frames N]                      rAF frames that count as "running" (30)
//     [--budget S]                      overall boot budget seconds (45)
//     [--dsf N]                         device-scale-factor (1)
//
// --keys grammar (steps separated by ';'; <Code> = DOM KeyboardEvent.code):
//   hold:<Code>:<ms>   press <Code>, hold <ms> (re-emitting keydown ~every 100ms
//                      for SDL auto-repeat paths), then release
//   tap:<Code>         quick press+release
//   down:<Code>        raw keydown (for overlapping holds)
//   up:<Code>          raw keyup
//   wait:<ms>          sleep <ms>
//   e.g. "hold:KeyW:2500;tap:Space;hold:KeyD:800"
//
// --assert-complete is the flood detector. After settle+keys+burst it decodes a
// screenshot and computes the fraction of pixels held by the single most common
// exact RGB triple. A complete gameplay frame is richly varied (dominant colour
// well under 90%); a flood frame is ~one colour (>90%). It ALSO fails on a
// visible #fatal panel or a "Boot failed" gate message. When --burst is combined
// with --assert-complete, EVERY burst frame is flood-checked too (the worst
// fraction wins) so a transient mid-hold flicker is caught, not just the final
// resting frame.

import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { readFile, writeFile, mkdtemp, rm } from "node:fs/promises";
import { existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, extname, dirname, basename } from "node:path";
import zlib from "node:zlib";

// ---- args -----------------------------------------------------------------
function argVal(name, def) {
  const i = process.argv.indexOf(name);
  return i !== -1 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
const DIST = argVal("--dist");
const ROM = argVal("--rom");
const CHROME = argVal("--chrome");
const OUT = argVal("--out", "webcap.png");
const QUERY = argVal("--query", "");
const SETTLE = Number(argVal("--settle-ms", "8000"));
const WIN = argVal("--window", "1280,800").replace("x", ",");
const THROTTLE = Number(argVal("--throttle", "0"));
const KEYS = argVal("--keys", "");
const BURST = argVal("--burst", "");                    // "N:M"
const ASSERT_COMPLETE = argVal("--assert-complete", ""); // "" = off; else percent
const FRAMES = Number(argVal("--frames", "30"));
const BUDGET_MS = Number(argVal("--budget", "45")) * 1000;
const DSF = argVal("--dsf", "1");

if (!DIST || !ROM || !CHROME) {
  console.error("usage: webcap.mjs --dist <dir> --rom <path> --chrome <bin> [--out png] [--query q] [--throttle N] [--keys spec] [--burst N:M] [--assert-complete T]");
  process.exit(2);
}

const log = (...a) => console.log("[webcap]", ...a);
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
      const full = join(root, p);   // path only — ?v=HASH etc. ignored; confine to root
      if (!full.startsWith(root) || !existsSync(full)) { res.writeHead(404); res.end("not found"); return; }
      res.writeHead(200, { "content-type": MIME[extname(full)] || "application/octet-stream" });
      res.end(await readFile(full));
    } catch (e) { res.writeHead(500); res.end(String(e)); }
  });
  return new Promise((resolve) => server.listen(0, "127.0.0.1", () => resolve({ server, port: server.address().port })));
}

// ---- minimal CDP-over-pipe client -----------------------------------------
class CDP {
  constructor(child) {
    this.wr = child.stdio[3];    // Chrome reads its input from fd 3
    this.rd = child.stdio[4];    // Chrome writes its output to fd 4
    this.id = 0;
    this.pending = new Map();
    this.listeners = [];
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

// ---- PNG decode (built-in zlib) -------------------------------------------
// Returns { width, height, channels, pixels } with scanlines un-filtered.
function pngDecode(png) {
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
  return { width, height, channels, pixels: out };
}

// Mean luminance over RGB (ignore alpha) — kept for parity with boot-smoke and
// as a cheap secondary signal in the log line.
function meanLuma({ pixels, channels }) {
  let sum = 0, n = 0;
  for (let i = 0; i < pixels.length; i += channels) {
    if (channels >= 3) sum += 0.299 * pixels[i] + 0.587 * pixels[i + 1] + 0.114 * pixels[i + 2];
    else sum += pixels[i];
    n++;
  }
  return n ? sum / n : 0;
}

// Dominant-colour fraction: the share of pixels held by the single most common
// exact RGB triple. This is the flood detector — a complete gameplay frame has
// many colours (dominant fraction low); a flood frame is ~one colour (~1.0).
// The colour map is capped at MAX_COLORS: hitting the cap already PROVES the
// frame has >MAX_COLORS distinct colours (nowhere near a flood), and a flood's
// dominant colour is inserted on scanline 0, long before the cap — so the cap
// never hides a real flood. Returns { fraction, dominant, distinct, capped }.
const MAX_COLORS = 4096;
function dominantColorFraction(dec) {
  const { pixels, channels, width, height } = dec;
  const total = width * height;
  const counts = new Map();
  let capped = false, overflow = 0;
  for (let i = 0; i < pixels.length; i += channels) {
    const key = channels >= 3
      ? ((pixels[i] << 16) | (pixels[i + 1] << 8) | pixels[i + 2]) >>> 0
      : pixels[i];
    const cur = counts.get(key);
    if (cur !== undefined) counts.set(key, cur + 1);
    else if (counts.size < MAX_COLORS) counts.set(key, 1);
    else { capped = true; overflow++; }
  }
  let max = 0, dominant = 0;
  for (const [k, v] of counts) if (v > max) { max = v; dominant = k; }
  const hex = "#" + (channels >= 3 ? dominant.toString(16).padStart(6, "0") : dominant.toString(16));
  return { fraction: total ? max / total : 0, dominant: hex, distinct: counts.size, capped, total, overflow };
}

// ---- lifecycle ------------------------------------------------------------
let child, udd, srv;
async function cleanup() {
  try { child && child.kill("SIGKILL"); } catch {}
  try { srv && srv.server.close(); } catch {}
  try { udd && await rm(udd, { recursive: true, force: true }); } catch {}
}
for (const s of ["SIGINT", "SIGTERM"]) process.on(s, () => cleanup().then(() => process.exit(130)));

async function evalOn(cdp, sess, expression) {
  const r = await cdp.send("Runtime.evaluate", { expression, returnByValue: true }, sess);
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
  return r.result.value;
}
async function waitFor(cdp, sess, expr, timeoutMs) {
  const t0 = Date.now();
  for (;;) {
    let v = false;
    try { v = await evalOn(cdp, sess, `!!(${expr})`); } catch {}
    if (v) return true;
    if (Date.now() - t0 > timeoutMs) return false;
    await sleep(250);
  }
}

// ---- key input ------------------------------------------------------------
// code -> [key, virtualKeyCode]. SDL2 in the browser reads e.code, so the code
// is what matters; key/vk are filled for completeness. Fallback derives a
// best-effort mapping for any KeyX / DigitN / other code not listed.
const KEYDEF = {
  KeyW: ["w", 87], KeyA: ["a", 65], KeyS: ["s", 83], KeyD: ["d", 68],
  KeyQ: ["q", 81], KeyE: ["e", 69], KeyR: ["r", 82], KeyF: ["f", 70],
  KeyC: ["c", 67], KeyM: ["m", 77], KeyH: ["h", 72],
  Space: [" ", 32], Enter: ["Enter", 13], Escape: ["Escape", 27], Tab: ["Tab", 9],
  ShiftLeft: ["Shift", 16], ShiftRight: ["Shift", 16],
  ControlLeft: ["Control", 17], ControlRight: ["Control", 17],
  ArrowUp: ["ArrowUp", 38], ArrowDown: ["ArrowDown", 40],
  ArrowLeft: ["ArrowLeft", 37], ArrowRight: ["ArrowRight", 39],
};
function keyInfo(code) {
  if (KEYDEF[code]) return KEYDEF[code];
  let m;
  if ((m = /^Key([A-Z])$/.exec(code))) return [m[1].toLowerCase(), m[1].charCodeAt(0)];
  if ((m = /^Digit([0-9])$/.exec(code))) return [m[1], 48 + Number(m[1])];
  return [code, 0];
}

// ---- main -----------------------------------------------------------------
let failed = false;
function fail(msg) { failed = true; console.error("[webcap] FAIL:", msg); }

try {
  srv = await startServer(DIST);
  const url = `http://127.0.0.1:${srv.port}/${QUERY ? "?" + QUERY : ""}`;
  log("serving", DIST, "->", url);

  udd = await mkdtemp(join(tmpdir(), "mgb64-webcap-"));
  child = spawn(CHROME, [
    "--headless=new", `--user-data-dir=${udd}`, "--no-first-run",
    "--no-default-browser-check", "--disable-extensions", "--remote-debugging-pipe",
    "--enable-unsafe-webgpu", "--mute-audio", `--window-size=${WIN}`,
    `--force-device-scale-factor=${DSF}`,
    "--disable-background-timer-throttling", "--disable-renderer-backgrounding",
    "--disable-backgrounding-occluded-windows", "about:blank",
  ], { stdio: ["ignore", "ignore", "ignore", "pipe", "pipe"] });
  const cdp = new CDP(child);

  const errs = [];
  cdp.on((method, params) => {
    if (method === "Runtime.exceptionThrown") {
      const d = params.exceptionDetails;
      errs.push("pageerror | " + (d.exception?.description || d.text || "exception"));
    } else if (method === "Runtime.consoleAPICalled" && (params.type === "error" || params.type === "warning")) {
      errs.push("console." + params.type + " | " + (params.args || []).map((a) => a.value ?? a.description ?? "").join(" "));
    }
  });

  const { targetId } = await cdp.send("Target.createTarget", { url: "about:blank" });
  const { sessionId: sess } = await cdp.send("Target.attachToTarget", { targetId, flatten: true });
  await cdp.send("Page.enable", {}, sess);
  await cdp.send("Runtime.enable", {}, sess);
  await cdp.send("DOM.enable", {}, sess);

  // Count rAF frames from before any page script runs (mirrors boot-smoke), so
  // "running" means the engine's rAF loop is actually advancing.
  await cdp.send("Page.addScriptToEvaluateOnNewDocument", {
    source: `(() => { window.__mgb64_raf = 0;
      const orig = window.requestAnimationFrame.bind(window);
      window.requestAnimationFrame = (cb) => orig((t) => { window.__mgb64_raf++; return cb(t); });
    })();`,
  }, sess);

  if (THROTTLE > 1) {
    await cdp.send("Emulation.setCPUThrottlingRate", { rate: THROTTLE }, sess);
    log("CPU throttle x" + THROTTLE + " (load-bearing: PERF-005 bleed only reproduced under throttle)");
  }

  await cdp.send("Page.navigate", { url }, sess);
  await waitFor(cdp, sess, "document.readyState === 'complete'", 15000);

  // gate → ROM → Play (reuse the boot-smoke flow).
  if (!(await waitFor(cdp, sess, "!document.getElementById('rom-ui').hidden", 20000)))
    throw new Error("capability gate never passed: " + (await evalOn(cdp, sess, "document.getElementById('gate-msg').textContent")));
  const doc = await cdp.send("DOM.getDocument", { depth: 1 }, sess);
  const { nodeId } = await cdp.send("DOM.querySelector", { nodeId: doc.root.nodeId, selector: "#rom-input" }, sess);
  await cdp.send("DOM.setFileInputFiles", { files: [ROM], nodeId }, sess);
  if (!(await waitFor(cdp, sess, "!document.getElementById('play').disabled", 15000)))
    throw new Error("ROM not accepted: " + (await evalOn(cdp, sess, "document.getElementById('rom-status').textContent")));
  await cdp.send("Runtime.evaluate", { expression: "document.getElementById('play').click()", userGesture: true }, sess);
  log("clicked Play; booting (budget " + (BUDGET_MS / 1000) + "s, throttle x" + (THROTTLE || 1) + ")…");

  // reach running: canvas visible, gate hidden, >=FRAMES rAF, no fatal/bootfail.
  const classify = `(() => {
    const g = document.getElementById('gate-msg').textContent;
    const fatal = document.getElementById('fatal');
    if (fatal && !fatal.hidden) return 'fatal:' + fatal.textContent;
    if (/^Boot failed/.test(g)) return 'bootfail:' + g;
    const canvas = document.getElementById('canvas');
    if (!canvas.hidden && document.getElementById('gate').hidden && window.__mgb64_raf >= ${FRAMES}) return 'running';
    return 'pending:raf=' + window.__mgb64_raf;
  })()`;
  const t0 = Date.now();
  for (;;) {
    let cls = "pending";
    try { cls = await evalOn(cdp, sess, classify); } catch (e) { cls = "eval-error:" + e.message; }
    if (cls === "running") break;
    if (cls.startsWith("fatal:")) throw new Error("fatal panel during boot: " + cls.slice(6));
    if (cls.startsWith("bootfail:")) throw new Error("engine boot failed: " + cls.slice(9));
    if (Date.now() - t0 > BUDGET_MS) throw new Error("engine never reached running state within budget. last: " + cls);
    await sleep(300);
  }
  log("running (" + ((Date.now() - t0) / 1000).toFixed(1) + "s)");

  log("settling " + SETTLE + "ms");
  await sleep(SETTLE);

  // ---- scripted key input ----
  async function key(type, code) {
    const [keyName, vk] = keyInfo(code);
    await cdp.send("Input.dispatchKeyEvent", { type, code, key: keyName,
      windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk }, sess);
  }
  if (KEYS) {
    log("keys:", KEYS);
    for (const step of KEYS.split(";").map((s) => s.trim()).filter(Boolean)) {
      const [op, a, b] = step.split(":");
      if (op === "hold") {
        await key("rawKeyDown", a);
        const tEnd = Date.now() + Number(b);
        while (Date.now() < tEnd) { await sleep(100); await key("rawKeyDown", a); } // auto-repeat
        await key("keyUp", a);
        await sleep(120);
      } else if (op === "tap") { await key("rawKeyDown", a); await sleep(60); await key("keyUp", a); await sleep(60); }
      else if (op === "down") await key("rawKeyDown", a);
      else if (op === "up") await key("keyUp", a);
      else if (op === "wait") await sleep(Number(a));
      else log("unknown key step:", step);
    }
  }

  // capture helper → returns decoded flood stats (or null on decode failure).
  const outDir = dirname(OUT), outBase = basename(OUT).replace(/\.png$/i, "");
  async function capture(path) {
    const { data } = await cdp.send("Page.captureScreenshot", { format: "png" }, sess);
    const buf = Buffer.from(data, "base64");
    if (path) await writeFile(path, buf);
    try { const dec = pngDecode(buf); return { ...dominantColorFraction(dec), luma: meanLuma(dec) }; }
    catch (e) { log("screenshot decode failed:", e.message); return null; }
  }

  // Track the worst (highest) dominant fraction seen across every captured frame
  // so --assert-complete catches a transient flood, not only the resting frame.
  let worst = null;
  const consider = (stat, label) => {
    if (!stat) return;
    if (!worst || stat.fraction > worst.fraction) worst = { ...stat, label };
  };

  // ---- burst (flicker hunt) ----
  if (BURST) {
    const [n, m] = BURST.split(":").map(Number);
    log("burst " + n + " x " + m + "ms");
    for (let i = 0; i < n; i++) {
      const p = join(outDir, `${outBase}_burst_${String(i).padStart(3, "0")}.png`);
      consider(await capture(p), "burst#" + i);
      if (i < n - 1) await sleep(m);
    }
  }

  // ---- final screenshot + flood assertion ----
  const finalStat = await capture(OUT);
  consider(finalStat, "final");
  if (finalStat)
    log("wrote", OUT, "| dominant " + finalStat.dominant + " " + (finalStat.fraction * 100).toFixed(1) +
        "% of pixels, " + finalStat.distinct + (finalStat.capped ? "+" : "") + " colours, luma " + finalStat.luma.toFixed(1));

  if (ASSERT_COMPLETE !== "") {
    const T = Number(ASSERT_COMPLETE);
    // A late fatal/bootfail is a completeness failure too.
    const late = await evalOn(cdp, sess, classify);
    if (typeof late === "string" && (late.startsWith("fatal:") || late.startsWith("bootfail:")))
      fail("engine failed after boot: " + late);
    if (!worst) fail("no screenshot could be decoded for the completeness assertion");
    else {
      const pct = worst.fraction * 100;
      if (pct > T) {
        fail("INCOMPLETE frame (" + worst.label + "): single colour " + worst.dominant + " fills " +
             pct.toFixed(1) + "% of pixels (> " + T + "% threshold) — flood/bleed frame presented");
      } else {
        log("complete-frame OK: worst single-colour share " + pct.toFixed(1) + "% (" + worst.label +
            ", " + worst.dominant + ") <= " + T + "% threshold");
      }
    }
  }

  if (errs.length) { log("console diagnostics (" + errs.length + "):"); for (const e of errs) console.log("   ", e); }
} catch (e) {
  fail((e && e.stack) || String(e));
}
await cleanup();
if (failed) { console.error("[webcap] RESULT: FAIL"); process.exit(1); }
console.log("[webcap] RESULT: OK");
process.exit(0);
