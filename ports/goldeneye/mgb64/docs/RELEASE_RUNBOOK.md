# MGB64 Release Runbook

The single **operational sequence** the owner follows to ship a release. It cuts
(A) the next native app release (macOS / Windows / Linux / PortMaster) and (B)
the GitHub Pages web demo, in order, with the exact command or click-path for
every step.

This runbook **sequences and cites** the authoritative scripts and checklists —
it does not restate their internals. The load-bearing references are
`docs/RELEASING.md` (owner checklists), `docs/WEB.md` (browser demo),
`docs/BACKEND_DEPRECATION_PLAN.md` (why this is a *proving* release), and
`docs/audit/HARNESS_STRATEGY.md` §8 E5 (owner-only handoff). Exact file:line
citations for every claim are in the **Appendix: verified-claims table** at the
end.

**Doctrine that shapes every step (do not deviate):**
- **Guarded publish only.** There is no `git push origin main`; every public push
  goes through `scripts/publish_public.sh` (or the `.githooks/pre-push` backstop).
- **Owner gameplay gates the release**, structurally, on **macOS AND Windows**
  (`--confirm-gameplay`) — not a printed reminder.
- **Bring-your-own-ROM.** Nothing built, packaged, or published contains ROM or
  ROM-derived data; the asset-free guards enforce it.
- **Single public lineage.** `akratch/mgb64` is the one source of truth (the old
  private staging fork is retired/archived).
- **This is the WebGPU *proving* release** — GL + Metal fallbacks ship intact.
  No backend deletion happens in this release (see Phase 5).

**Version for this cut:** `MGB64_VERSION` is single-sourced in CMake
(`CMakeLists.txt:33`) and threaded to the CI build jobs via `-DMGB64_VERSION`.
`RELEASE_NOTES.md` already carries the drafted **`v0.4.0`** stable header (tags
today stop at `v0.4.0-alpha.3`), so the next cut is **`v0.4.0`**. Throughout this
runbook substitute your actual tag for `<v>` (e.g. `v0.4.0`) and `akratch/mgb64`
for `<repo>`.

---

## Phase 0 — Preflight (repo state green)

**Goal:** the release commit is integrated on `main`, version-bumped, changelog
current, and every gate that can run without owner hardware is green.

### 0.1 Land the branch on `main` (normal PR flow)

This repo develops in the open; `main` is protected and advances only through a
PR merge (`docs/RELEASING.md` "Developing in the open"). `feat/webgpu-backend` is
unpushed — integrate it the ordinary way:

```sh
git push -u origin feat/webgpu-backend        # goes through .githooks/pre-push leak scan
gh pr create --base main --fill
gh pr merge --rebase --delete-branch          # self-merge is fine (0 required approvals)
```

- **Expected:** the PR merges; `main` now contains the release commit. The
  pre-push hook scans the exact pushed commits for the never-leak classes and
  must not fire.
- **Failure:** if the hook refuses, it named a never-leak hit (private path,
  personal email, credential, ROM/media, SDK-notice text) — fix the content, do
  **not** bypass the hook.

### 0.2 Bump version + update the changelog

- Confirm `RELEASE_NOTES.md`'s top section matches `<v>` and names **WebGPU as
  the default renderer** plus the fallback env vars (`GE007_RENDERER=gl|metal`,
  `-DMGB64_WEBGPU_BACKEND=OFF`) and the **macOS unsigned right-click → Open**
  note. (`docs/RELEASING.md` renderer section requires this.)
- Version is passed at build time (`-DMGB64_VERSION=<v>`); there is no source
  constant to hand-edit — the CMake default stays `0.0.0-dev` for dev builds.

### 0.3 Source-hygiene + full local gate (owner ROM present)

Run the maintainer preflight from a clean tree with the ROM kept **outside** the
repo (`docs/RELEASE_CHECKLIST.md` "Preferred Preflight"):

```sh
scripts/release_preflight.sh --deep-runtime \
  --rom /path/outside/repo/baserom.u.z64 \
  --macos-app-bundle-sdl2 --strict-ignored --github
```

Then the strict full-coverage verify for the release commit (the publish gate
rejects a tier-limited report):

```sh
GE007_VERIFY_STRICT=1 tools/fidelity/verify_all.sh      # produces docs/fidelity/reports/verify_<sha>.json, verdict green
```

- **Expected:** preflight passes; a **green** `verify_<sha>.json` for HEAD.
- **Gotchas (from memory / HARNESS §9):** ctest is Release `-DNDEBUG` so
  `assert()` is stripped; run route recaptures **sequentially** (tape lock
  contention); ROM-gated smokes silently skip without a ROM, so only a full local
  ROM run truly exercises `port_campaign_route_smoke`. Two known **pre-existing**
  smoke fails at parent (train_aperture render-sensitivity + campaign_route stale
  fixtures, AUDIT-0017) are not introduced by this branch — confirm any smoke fail
  is not new before treating it as a blocker.

### 0.4 Windows-RUNS execution gate (dispatch + green)

`windows-validate.yml` is `workflow_dispatch`-only and had **never been
dispatched** at audit time — it is a hard precondition, not optional. Dispatch it
against the **release commit** and confirm green *before* cutting:

```sh
gh workflow run windows-validate.yml --ref main
gh run watch                                   # or: Actions → "Windows validate (execution)"
```

- **Expected green jobs:** build on `windows-latest`/MSYS2, **import-table guard**
  (self-contained: system DLLs + `SDL2.dll` only), **headless execution smoke**
  (config schema self-check, `--dump-config`, `--list-settings`, update-check
  self-test), and the **ROM-free CTest subset**
  (`arg_triage|rom_validate|update_check|port_env|sim_state_hash|room_normals`).
- **Failure:** an import-table hit means a non-self-contained `.exe`
  (`libgcc_s_seh`/`libstdc++`/`libwinpthread` leak) — **stop**; it would die
  "DLL not found" on a player's PC.

**Phase 0 exit:** `main` = release commit; green preflight + strict verify;
green `windows-validate.yml` on the release commit.

---

## Phase 1 — Owner attestation gates (macOS + Windows gameplay)

**Goal:** the irreducible human gate. Both are structurally required by the
publish gate and by `scripts/release.sh --publish` (`--confirm-gameplay` with
`macos=` AND `windows=` each `<initials>/<date>`). Play the game; eyeball the
claims; record initials/date.

### 1.1 macOS gameplay (the developed platform)

Boot the WebGPU-default build and play through real gameplay. Eyeball:
- **WebGPU is the live default** (no `GE007_RENDERER` override) and boots to game.
- **Post-FX / `--remaster` look on WebGPU** — FXAA/CAS/tonemap/grade/vignette/
  bloom render, and **SSAO runs on WebGPU** (planar v1). This is the concrete
  form of the deprecation Phase-M precondition, and the macOS attestation here is
  what later gates the Metal deletion (see Phase 5).
- **Fallback escape hatches still work:** `GE007_RENDERER=gl` and
  `GE007_RENDERER=metal` each still boot (they must remain selectable this
  release).
- Tape byte-exact + parity within tolerance if you run the fidelity captures.

Record e.g. `macos=AK/2026-07-16`.

### 1.2 Windows gameplay

On real Windows, run the CI-built portable `.zip` (`ge007.exe` + `SDL2.dll`) with
your ROM; play briefly; confirm boot + input + audio. Record e.g.
`windows=AK/2026-07-16`.

### 1.3 What `--confirm-gameplay` records

The attestation string
`--confirm-gameplay "macos=<initials/date>,windows=<initials/date>"` is parsed and
**shape-validated** by both `scripts/publish_public.sh` (release push) and
`scripts/release.sh --publish` (asset attach): each key must be present and each
value must be `<initials>/<date>`. A missing or lazily-shaped attestation is
**refused at the script**, not merely warned. Keep the exact same string for both
the tag push (Phase 3.2) and the asset attach (Phase 3.4).

---

## Phase 2 — Signing (first release with the scaffold)

Two independent signing layers exist; **both are deferred-by-default** and the
release ships fine without either (plain SHA256SUMS, ad-hoc macOS app). Do this
phase only when minting the first *signed* release.

### 2.1 Apple notarization (macOS `.app`) — DEFERRED per doctrine

Developer-ID `--sign` is on record as **deferred**. Unsigned macOS ships with the
**right-click → Open** first-launch note in the release notes. The full local
flow (Developer ID cert, `APPLE_ID`/`APPLE_TEAM_ID`/`APPLE_APP_PASSWORD`, then
`scripts/release.sh --sign`) is documented in `docs/RELEASING.md` "Code signing +
notarization" if/when the enrollment decision flips. **This runbook assumes
unsigned macOS** unless the owner has enrolled.

### 2.2 Manifest signing (minisign) — first-signed-release setup

The manifest-signing key is minisign, minted and kept **only on the owner's Mac**
(no CI secret). The scaffold is fail-closed and inert until the real key lands;
`scripts/release/mgb64-release-pubkey.txt` is a committed **placeholder** and
`verify_release.sh` refuses to run against it.

One-time key mint + pubkey swap:

```sh
brew install minisign
minisign -G -p mgb64-release.pub -s ~/.minisign/mgb64-release.key   # choose a passphrase
# Replace the placeholder pubkey file with the generated public key, and commit it (it is public):
cp mgb64-release.pub scripts/release/mgb64-release-pubkey.txt
git add scripts/release/mgb64-release-pubkey.txt
git commit -m "release: publish minisign public key for signed releases"
```

Export for the signed publish (passphrase is optional; it is piped to minisign's
stdin, never echoed):

```sh
export MGB64_MANIFEST_SIGNING_KEY="$HOME/.minisign/mgb64-release.key"
export MGB64_MANIFEST_PASSPHRASE="..."      # optional; else minisign prompts
```

Then add `--sign-manifest` to the Phase 3.4 publish command. `release.sh` fails
fast (before the build) if the key path or the minisign binary is missing; it
signs `mgb64-manifest-<v>.json` and `mgb64-SHA256SUMS-<v>.txt` **after** the
provenance gate passes, producing the `.minisig` files that ship with the
release.

### 2.3 User-side verify smoke (prove the signature end-to-end)

After a signed publish (or against your local `dist/`), prove the verifier
against the produced artifacts:

```sh
scripts/release/verify_release.sh <download-folder> -P scripts/release/mgb64-release-pubkey.txt
```

- **Expected:** `VERIFIED` — both signatures valid, every listed asset digest
  matches, and it prints the bound source commit + version to compare against the
  tag's target on the release page.
- **Failure is fail-closed at every step:** bad signature, tampered
  manifest/checksums/asset, missing `.minisig`, missing minisign binary, or the
  **placeholder** pubkey all exit nonzero with a clear message.

---

## Phase 3 — Native release cut

**Goal:** assemble Windows+Linux (CI) + macOS (local), verify provenance, push
the tag through the gate, and attach assets to the public release.

### 3.1 Build Windows + Linux via release CI

`release.yml` is `workflow_dispatch`-only. Dispatch against the release commit
with the version label (it threads `-DMGB64_VERSION`):

```sh
gh workflow run release.yml --ref main -f version=<v>
gh run watch
```

- **Expected artifacts:** `mgb64-windows-<v>` (portable `.zip`), `mgb64-linux-<v>`
  (AppImage + `.tar.gz`), each carrying a `.provenance.json` sidecar stamped to
  this commit; the **PortMaster GLES compile check** job green.
- Download both artifact sets into `dist/`:

```sh
gh run download <run-id> --dir dist         # then flatten so files sit directly in dist/
```

- **Failure:** a red **PortMaster GLES** job blocks the release (handheld target
  stopped compiling). The Linux job also headless-smokes the launcher
  (`smoke: rendered 30 frames`).

### 3.2 Push the release tag through the gate (git history) — FIRST

The tag must be on the public remote **before** `release.sh` publishes; the
publish uses `gh release create --verify-tag` and **will not mint the tag
server-side**. This is the only path that puts the tag on the remote (strict
verify + gameplay gate):

```sh
git tag -a <v> -m "MGB64 <v>"
scripts/publish_public.sh --tag <v> \
  --confirm-gameplay "macos=<initials/date>,windows=<initials/date>" --yes
```

- Without `--yes` it is a **dry run** that prints the exact rev range. It enforces
  clean tree → hygiene guards → strict verify green → gameplay attestation → never
  force-push, in that order, refusing on any failure.
- **Failure (non-green verify):** proceed only with an explicit owner
  adjudication `--verify-report <path> --red-note "<why>"` (logged into the push
  annotation).

### 3.3 Build + validate macOS locally (release.sh, no publish)

Build the arm64 `.app`, verify asset-free, zip, and stamp provenance — without
publishing, to stage `dist/`:

```sh
scripts/release.sh --version <v>            # host-arch macOS build; stages dist/mgb64-macos-<v>.zip
```

- **Expected:** `[release] macOS asset: dist/mgb64-macos-<v>.zip` plus a stamped
  `.provenance.json` sidecar; the script prints the next (publish) command.
- Notes: `--universal` needs a universal SDL2 (Homebrew SDL2 is single-arch → the
  shipped prebuilt is Apple-Silicon-only); `--sign` adds Developer-ID sign+notarize
  (deferred, Phase 2.1).

### 3.4 Publish (guard chain + provenance + attach)

With macOS + Windows + Linux assets all in `dist/`, publish. `--publish` runs the
publish guard chain itself and stops on red; it **structurally requires**
`--confirm-gameplay` and verifies provenance before attaching:

```sh
scripts/release.sh --version <v> --repo <repo> \
  --confirm-gameplay "macos=<initials/date>,windows=<initials/date>" --publish
# first SIGNED release: also add  --sign-manifest  (and --sign for Apple, if enrolled)
# rolling 'latest' prerelease instead of a tag:  add  --rolling-latest
```

What runs, in order (all fail-closed):
1. **Publish guard chain** — `scripts/publish_public.sh --dev-push` dry-run
   (clean tree + hygiene + strict verify).
2. **Gameplay gate** — the `--confirm-gameplay` shape check.
3. **Provenance binding** — `verify_provenance.sh` requires every `dist/` asset to
   carry a sidecar whose sha256 matches the file and whose commit == HEAD and
   version == `<v>`; it emits `mgb64-SHA256SUMS-<v>.txt` + `mgb64-manifest-<v>.json`.
4. **(if `--sign-manifest`)** minisign-sign the manifest + SHA256SUMS.
5. **Attach** every `dist/mgb64-*-<v>.*` (excluding the raw `.provenance.json`
   sidecars) to the tag via `gh release create --verify-tag` (or `upload
   --clobber` if the release already exists).

- **Expected:** `[release] done: https://github.com/<repo>/releases/tag/<v>`.
- **Failure (`no git tag '<v>'`):** you skipped Phase 3.2 — push the tag through
  the gate first, then re-run the publish.

### 3.5 Post-publish verification (clean-machine perspective)

From a clean folder, download the published assets + manifest + SHA256SUMS
(+ `.minisig` if signed) and verify:

```sh
# unsigned release: plain digest check
sha256sum -c mgb64-SHA256SUMS-<v>.txt
# signed release: full cryptographic verify (needs the published pubkey)
scripts/release/verify_release.sh <folder> -P "RW...<published key string>"
```

- Confirm the printed bound **commit** equals the tag's target commit on the
  release page, and that the README **Download** links (`/releases/latest`)
  resolve to the macOS/Windows/Linux assets.

**Phase 3 exit:** public release at `<v>` with all four platforms' assets +
SHA256SUMS + manifest (+ signatures if signed); post-publish verify clean.

---

## Phase 4 — Web demo deploy (GitHub Pages)

**Goal:** deploy the same engine compiled to wasm to GitHub Pages, owner-triggered.

### 4.1 One-time Pages setup

**Settings → Pages → Source → GitHub Actions.** Required once per repo before any
`web-demo.yml` run can deploy (tells Pages to accept a workflow artifact instead
of a branch). Skip only if already set.

### 4.2 Dispatch the deploy

`web-demo.yml` is `workflow_dispatch`-only (nothing auto-deploys on push):

```sh
gh workflow run web-demo.yml --ref main
gh run watch
```

- **Expected green jobs:** `build` (emsdk 4.0.10 → `tools/web/build_web.sh`),
  **size budget** (`ge007_web.wasm` < 40 MiB; current ~3.81 MiB), **ROM-absence
  guard** (site ships exactly 5 known files: `index.html`, `mgb64-shell.js`,
  `style.css`, `ge007_web.js`, `ge007_web.wasm`; fails on any `.z64`-magic or
  unexpected file), then `deploy` via `actions/deploy-pages`.
- The workflow holds only `contents: read` + `pages: write` + `id-token: write`
  (the OIDC token `deploy-pages` needs — no repo push access).

### 4.3 Live-URL smoke (Chrome + Safari 26, real ROM)

On the deployed Pages URL, on **both Chrome and Safari 26**:
1. **Capability gate** shows correctly (WebGPU + OPFS detected; no fallback by
   design — Firefox Linux/Android declines gracefully).
2. **Pick a real GoldenEye 007 (U) `.z64` (exactly 12 MB)** via the file input;
   confirm it **boots** and plays briefly.
3. **Reload** and confirm the stored ROM is picked back up from OPFS (no re-pick).
4. Confirm a **save persists across a reload** (IDBFS `/save`, flushed on a 5s
   timer + `pagehide`).
5. **Privacy promise:** confirm the ROM never leaves the browser — there is no
   server component; OPFS is origin-private. The CI ROM-absence guard already
   proves no ROM ships in the artifact.

### 4.4 Rollback

Pages has no separate "undo" — **re-dispatch `web-demo.yml` at the previous good
SHA** to redeploy the prior site:

```sh
gh workflow run web-demo.yml --ref <previous-good-sha>
```

**Phase 4 exit:** live Pages demo boots a real ROM on Chrome + Safari 26; ROM
persists; save persists; ROM-absence guard green.

---

## Phase 5 — Deprecation timeline hook (this is the PROVING release)

This release is the **proving release**: WebGPU is the default **and the GL +
Metal fallbacks ship intact and selectable** (`GE007_RENDERER=gl|metal`,
`-DMGB64_WEBGPU_BACKEND=OFF`). Per `docs/BACKEND_DEPRECATION_PLAN.md` the phase
order is a hard invariant: **never delete a fallback in the same release that
first ships the new default.** Phase M (Metal deletion — `gfx_metal.mm` + the
enumerated `src/` call sites, rehearsed at the preserved DO-NOT-MERGE branch
`8373ac6`) lands only in the release **after** this one, gated on the **macOS
WebGPU gameplay attestation from Phase 1.1** (the `--remaster`/SSAO-on-WebGPU
precondition is already MET, 2026-07-16). Phase G (desktop-GL) is later still and
gated on the handheld/PortMaster decision. **Do nothing deletion-related in this
release** — just record that the macOS attestation was captured, since it is the
Phase-M precondition. Pointer: `docs/BACKEND_DEPRECATION_PLAN.md` §2, §4.

---

## Phase 6 — Rollback / incident

### 6.1 Per-surface rollback

- **Native release (bad asset):** re-run `scripts/release.sh ... --publish` — it
  `gh release upload --clobber`s the corrected `dist/` assets onto the existing
  tag (provenance re-verified). For a bad *tag*, cut a **patch release**
  (`v0.4.1`) through the full gate rather than moving a published tag (no
  force-push; tags are protected). Deleting a published release is a `gh release
  delete <v> --repo <repo>` last resort — prefer a forward patch.
- **Rolling `latest`:** re-run with `--rolling-latest` to refresh the pointer from
  a known-good build.
- **Web demo:** re-dispatch `web-demo.yml` at the previous good SHA (Phase 4.4).

### 6.2 Support triage pointers

- **Renderer fallbacks are the field escape hatch:** ask a user hitting a WebGPU
  regression to try `GE007_RENDERER=gl` (all platforms) or `GE007_RENDERER=metal`
  (macOS) — both are release-supported this line.
- **Diagnostics:** `GE007_DEBUG` and the `GE007_*` env flags (`docs/ENV_FLAGS.md`,
  `docs/INSTRUMENTATION.md`); wasm rendering oddities → check the
  `[SEG_ADDR-ILP32]` telemetry line count (`docs/WEB.md` ILP32 notes).
- **Windows "won't start":** almost always the import-table class — reproduce with
  `windows-validate.yml` (Phase 0.4).

---

## Appendix: verified-claims table

Every operational claim above is grounded in source at the file:line below (read,
not assumed). Line anchors are against HEAD `4494a63`; re-check before executing.

| Claim | Source |
| --- | --- |
| `release.sh` flags: `--sign` / `--sign-manifest` / `--confirm-gameplay` / `--rolling-latest` / `--universal` | `scripts/release.sh:38,42-43,47,53-62,67-77` |
| `--publish` requires `--confirm-gameplay`; runs guard chain; refuses on red | `scripts/release.sh:142-166` |
| macOS build + asset-free + zip + provenance stamp | `scripts/release.sh:108-135` |
| Provenance gate (`verify_provenance.sh`) before attach; emits SHA256SUMS + manifest | `scripts/release.sh:200-210` |
| `--sign-manifest` minisign step (after provenance) | `scripts/release.sh:217-234` |
| `gh release create --verify-tag` — never mints the tag | `scripts/release.sh:255-268` |
| Provenance sidecar schema (commit/sha256/version/platform) | `scripts/release/stamp_provenance.sh:35-65` |
| `verify_provenance.sh` fails closed on missing/orphan/mismatch; emits manifest | `scripts/release/verify_provenance.sh:45-136` |
| `verify_release.sh` refuses placeholder pubkey; verifies both sigs + digests; prints bound commit | `scripts/release/verify_release.sh:70-84,126-153` |
| Pubkey is a committed placeholder until first signed release | `scripts/release/mgb64-release-pubkey.txt:3-9` |
| `release.yml` `workflow_dispatch` + version input; Linux/Windows/PortMaster jobs; provenance stamp; Linux headless smoke | `.github/workflows/release.yml:16-23,28-118,46-51,54-58` |
| `windows-validate.yml` `workflow_dispatch`; import-table guard; headless smoke; ROM-free CTest subset | `.github/workflows/windows-validate.yml:29-30,77-138` |
| `web-demo.yml` `workflow_dispatch`; pages/id-token perms; 40 MiB budget; ROM-absence 5-file guard; deploy-pages | `.github/workflows/web-demo.yml:2-8,17-45` |
| macOS release CI is asset-free-only; sign/notarize/DMG remain disabled | `.github/workflows/macos-release.yml:1-11,72-125` |
| Pages one-time setup (Settings→Pages→GitHub Actions); dispatch; deploy posture | `docs/WEB.md:170-199` |
| Web user flow: WebGPU+OPFS gate; 12 MB `.z64`; OPFS private; IDBFS save persist | `docs/WEB.md:14-64` |
| Publish gate order (clean tree→hygiene→strict verify→gameplay→no force-push) | `docs/RELEASING.md:117-174` |
| `publish_public.sh` flags: `--tag`, `--dev-push`, `--confirm-gameplay`, `--yes`, `--verify-report/--red-note` | `scripts/publish_public.sh:23-51,93-100` |
| Release cut sequence: CI first, tag through gate before `release.sh` | `docs/RELEASING.md:195-227` |
| Owner-gated tail: windows-validate green, minisign key, disposable-repo test, Pages once, web smoke, Apple deferred | `docs/RELEASING.md:341-397` |
| First signed release: `minisign -G`, swap pubkey, `--sign-manifest` | `docs/RELEASING.md:255-286` |
| `MGB64_VERSION` single-sourced, CI threads `-DMGB64_VERSION` | `CMakeLists.txt:27-38` |
| Proving-release-first invariant; Phase M gated on macOS attestation | `docs/BACKEND_DEPRECATION_PLAN.md:48-73,244-258` |
| Owner-only handoff (E5): signing key, gameplay attestation, Apple deferred | `docs/audit/HARNESS_STRATEGY.md:534-548` |
| Maintainer preflight + source-hygiene gate commands | `docs/RELEASE_CHECKLIST.md:7-45,60-71` |

---

*Operational companion to `docs/RELEASING.md` (checklists) and
`docs/RELEASE_CHECKLIST.md` (hygiene). When those and this runbook disagree, the
scripts they cite are authoritative — re-verify against source.*
