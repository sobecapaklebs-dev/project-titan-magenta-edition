// sw.js — PERF-036: cache-first service worker for the MGB64 browser demo.
//
// Why this exists: GitHub Pages serves every asset with a fixed
// `cache-control: max-age=600` and gives us no header control, so a returning
// player re-validates every file after 10 minutes and re-downloads the whole
// engine after each deploy. The ROM already lives in OPFS and saves in IDBFS —
// one service worker turns repeat visits into instant boots and makes the demo
// fully playable OFFLINE after the first successful visit.
//
// It ALSO closes the WEB-032 glue/wasm skew window more robustly than the shell's
// `?v=HASH` pairing: the whole app is precached as ONE atomic generation keyed by
// the build hash (see BUILD below), so a browser can never pair a cached
// index.html/shell from one build with wasm from another — a half-written new
// generation simply never activates.
//
// DESIGN
//   - Hash-versioned cache. CACHE_NAME embeds the same `__MGB64_BUILD__` stamp
//     build_web.sh already rewrites in the shell (extended to seed this file too),
//     so every deploy is a fresh, independent cache generation.
//   - Cache-first for the precached app; network passthrough for everything else.
//   - Atomic activate-on-complete: skipWaiting() runs ONLY after addAll() resolves,
//     so a partially-cached new build never takes over from a good old one.
//   - activate evicts every OTHER `mgb64-*` generation (old deploys) and claims
//     open clients so the next navigation is served entirely from the new cache.
//   - Same-origin GET only; cross-origin and non-GET requests are never touched.
//   - Console-quiet by contract: the browser boot-smoke lane fails on any
//     unexpected console error/warning, so this file logs nothing.
//
// NOTE: a service worker must NOT cache its own script — the browser fetches
// sw.js through its own update channel (bypassing the fetch handler), byte-
// compares it, and installs a new worker when the stamped BUILD changes. So sw.js
// is deliberately absent from PRECACHE and always served fresh from the network.

// Seeded by tools/web/build_web.sh in the STAGED dist copy only (same sed pass
// that stamps mgb64-shell.js); the source keeps the literal placeholder. The
// shell gates SW registration on this exact placeholder check, so an unstamped
// dev/ctest copy is never registered and this value is never observed live with
// the placeholder still in it.
const BUILD = "__MGB64_BUILD__";
const CACHE_NAME = "mgb64-" + BUILD;

// The deployed app, by CANONICAL (query-less) URL, relative to this worker's
// scope. sw.js itself is intentionally excluded (see file header). "./" is the
// navigation request (Pages / the dev server serve index.html for the dir root);
// index.html is cached too so a direct .../index.html navigation also hits.
// The shell requests ge007_web.js / ge007_web.wasm with a `?v=HASH` suffix
// (WEB-032); those are matched here with { ignoreSearch: true } — see fetch().
const PRECACHE = [
  "./",
  "index.html",
  "style.css",
  "mgb64-shell.js",
  "ge007_web.js",
  "ge007_web.wasm",
];

// install: precache the whole app, THEN skipWaiting — the atomicity guarantee.
// If any file fails to fetch, addAll() rejects, install fails, skipWaiting is
// never reached, and this worker never activates: the previous good generation
// keeps serving. No half-cached build is ever promoted.
self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => cache.addAll(PRECACHE))
      .then(() => self.skipWaiting())
  );
});

// activate: evict every OTHER mgb64-* generation (previous deploys), then claim
// open clients. The prefix filter is load-bearing on GitHub Pages, where the
// origin (akratch.github.io) is SHARED across projects and CacheStorage is
// per-origin: we must delete only our own stale caches, never a sibling site's.
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(
        keys
          .filter((k) => k.startsWith("mgb64-") && k !== CACHE_NAME)
          .map((k) => caches.delete(k))
      ))
      .then(() => self.clients.claim())
  );
});

// fetch: same-origin GET → cache-first, network fallback. Everything else is left
// entirely to the browser (no respondWith) — cross-origin requests are never
// intercepted, and non-GET (e.g. never happens here, but be strict) passes through.
//
// ignoreSearch is the `?v=HASH` decision (documented in docs/WEB.md): the shell
// requests the versioned engine files as ge007_web.js?v=HASH / .wasm?v=HASH, but
// they are precached under their canonical query-less URLs, so a `?v=` request is
// matched with the search string ignored and still HITS the cache. It also lets a
// dev/test navigation carrying `?arg=...` (the shell's URL passthrough) resolve to
// the cached shell offline. ignoreSearch only ignores the query — paths must match
// exactly, so ge007_web.wasm?v=x can never alias ge007_web.js.
self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.method !== "GET") return;
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;
  event.respondWith(
    caches.open(CACHE_NAME).then((cache) =>
      cache.match(req, { ignoreSearch: true }).then((hit) => {
        if (hit) return hit;
        // A navigation that missed (e.g. offline deep-load to an uncached path)
        // still boots the app from the cached shell; other misses go to network.
        if (req.mode === "navigate")
          return cache.match("./", { ignoreSearch: true }).then((idx) => idx || fetch(req));
        return fetch(req);
      })
    )
  );
});
