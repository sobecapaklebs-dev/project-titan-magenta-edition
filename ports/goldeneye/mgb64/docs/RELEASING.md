# Releasing MGB64 (prebuilt apps)

How the prebuilt **MGB64** apps for macOS, Windows, and Linux are built,
packaged, and published. One portable codebase (`ge007` with `MGB64_APP=ON`)
produces all three; per-platform binding details are in
**[APP_ARCHITECTURE.md](APP_ARCHITECTURE.md)**.

> **Operational sequence: see [RELEASE_RUNBOOK.md](RELEASE_RUNBOOK.md)** — the
> end-to-end, phase-by-phase runbook (native + GitHub Pages) that sequences the
> checklists and scripts below into executable steps.

Everything here is **asset-free** — no ROM or ROM-derived data is ever built,
packaged, or published (bring-your-own-ROM).

---

## Renderer

Builds of this tree render on **WebGPU** (wgpu-native) by default on every
platform — it maps to Metal on macOS, D3D12 on Windows, and Vulkan on
Linux/handhelds under the hood. **No published release contains WebGPU yet**
(the flip landed on the unpushed `feat/webgpu-backend` branch); the next
release is the proving release that ships it as the default. Two legacy
backends remain as **release-supported runtime fallbacks** and are selectable
without a rebuild:

- `GE007_RENDERER=gl` (or `opengl`) — OpenGL.
- `GE007_RENDERER=metal` — native Metal (macOS only; `--remaster` no longer
  pins this on Apple — the pin was retired 2026-07-16 once SSAO/post-FX
  landed on WebGPU at GL parity, see `docs/BACKEND_DEPRECATION_PLAN.md`.
  `--remaster` now runs on whatever backend is selected, WebGPU by default;
  `GE007_RENDERER=metal` is still explicitly selectable until Phase M).
- `-DMGB64_WEBGPU_BACKEND=OFF` at build time — a GL/Metal-only binary with no
  wgpu dependency.

These fallbacks are **supported for the current release line as escape hatches**;
they are scheduled for staged removal only after WebGPU proves out in a shipped
release. The retirement plan (Phase M: Metal; Phase G: desktop GL, kept alive on
handhelds via the shared `gfx_opengl.c`/GLES path) is
`docs/BACKEND_DEPRECATION_PLAN.md`. Until those phases complete, the fallback env
vars above are documented, release-supported behavior. `RELEASE_NOTES.md` should
name WebGPU as the default renderer and mention the fallback env vars.

The browser build (GitHub Pages demo) depends only on this WebGPU renderer —
see [WEB.md](WEB.md) for the full user/developer/deploy flow.

---

## The model: hybrid local + free CI

Public repos get free GitHub Actions runners, so cost is not a factor — the only
constraint is this repo's deliberate **local-preflight** provenance posture
(hosted workflows are `workflow_dispatch`-only; they never auto-run and never
write repository state). Releases are therefore assembled from two sources:

| Platform | Built by | Why |
| --- | --- | --- |
| **macOS** (Apple Silicon) | **Locally** on the maintainer's Mac (`scripts/release.sh`) | This is the developed + tested platform; it also needs the local Apple toolchain. Homebrew SDL2 is single-arch, so the shipped `.app` is arm64-only (`build_gl_app.sh --universal` can produce a universal build when a universal SDL2 is present). |
| **Windows** (portable `.zip`) | **CI** (`.github/workflows/release.yml`, MSYS2/MinGW) | Can't be built natively on macOS. |
| **Linux** (AppImage + `.tar.gz`) | **CI** (`.github/workflows/release.yml`, ubuntu-22.04) | Can't be built natively on macOS. |

The CI build jobs are asset-free and write no repo state — they only compile the
public source and upload the results as workflow artifacts, so they stay
consistent with the provenance policy.

---

## Build the app from source (any platform)

Full per-OS toolchain + build commands are in
[APP_ARCHITECTURE.md §4](APP_ARCHITECTURE.md#4-build). In brief:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMGB64_APP=ON
cmake --build build -j          # produces ./build/ge007 (the launcher)
```

Package it for distribution:

| Platform | Command | Output |
| --- | --- | --- |
| macOS | `./macos/Scripts/build_gl_app.sh [--universal]` | `build-macos-app/MGB64.app` (asset-free verified, ad-hoc signed) |
| Linux | `./scripts/package_linux_appimage.sh --binary build/ge007 --version vX.Y.Z` | `dist/mgb64-linux-vX.Y.Z.{AppImage,tar.gz}` |
| Windows (MSYS2) | `./scripts/package_windows_zip.sh --binary build/ge007.exe --version vX.Y.Z` | `dist/mgb64-windows-vX.Y.Z.zip` |

Every packager and the bundler run `verify_asset_free.sh` on the binary.

---

## Code signing + notarization (macOS)

Requires an active [Apple Developer Program](https://developer.apple.com/programs)
enrollment ($99/yr; Individual is fine for a solo maintainer). Signing runs
**locally**, alongside the rest of the macOS build — it is deliberately not
wired into hosted CI, so the Developer ID private key and Apple credentials
never need to leave this machine (see "The model" above).

One-time setup:

1. In Xcode → Settings → Accounts (or the [Certificates page](https://developer.apple.com/account/resources/certificates/list)),
   create a **Developer ID Application** certificate (not "Apple Distribution" —
   that one's for the App Store). This installs the cert + private key into
   your login keychain.
2. Note your **Team ID** (Membership page) and identity string, e.g.
   `Developer ID Application: Jane Doe (ABCDE12345)`.
3. Generate an app-specific password at
   [appleid.apple.com](https://appleid.apple.com) → Sign-In and Security →
   App-Specific Passwords, for `notarytool` to authenticate with.

Export these before running a signed release:

```sh
export DEVELOPER_ID_APPLICATION="Developer ID Application: Jane Doe (ABCDE12345)"
export APPLE_ID="you@example.com"
export APPLE_TEAM_ID="ABCDE12345"
export APPLE_APP_PASSWORD="xxxx-xxxx-xxxx-xxxx"   # or @keychain:label
```

Then pass `--sign` to `scripts/release.sh` (see below). Under the hood this
calls `macos/Scripts/sign_and_notarize.sh`, which signs the app bundle,
submits it to Apple's notary service, waits for approval, and staples the
ticket — all before the `.zip` is created, so the staple travels with it.
Without `--sign`, the app ships ad-hoc signed and Gatekeeper will warn
end users on first launch.

---

## The publish gate: `scripts/publish_public.sh`

Every push to the public remote — routine `main` update or release — goes through
**one guarded entrypoint**, `scripts/publish_public.sh`. There is no raw
`git push origin main`. This is the mechanized, forward-only boundary the owner
ruled for on 2026-07-11 (recorded in `docs/fidelity/ESCALATIONS.md`, resolved
C1/D1): the already-public history is accepted **as-is** — the script does **not**
rewrite history and does **not** filter content — and safety comes from guards
that run on every push.

**The structural backstop is the `.githooks/pre-push` hook** (install once with
`scripts/install_git_hooks.sh`). For *any* push whose remote is the public repo —
crucially including a day-to-day `git push -u origin fix/whatever` topic branch,
the lane whose subsequent server-side PR merge advances `main` without this
script ever running — the hook scans the exact pushed commits (content **and**
messages) for the never-leak classes and refuses on a hit. So the leak boundary
is enforced at the git layer for every configured clone, not only when this
script is run by hand. `publish_public.sh` runs the same ranged scan itself
(belt-and-suspenders) and the hook nudges `main`/tag pushes here for the strict
verify + gameplay gate.

What it enforces, in order, and refuses on any failure:

1. **Clean tree** — a dirty working tree is refused (the push must match what the
   guards + verify report were computed against).
2. **Hygiene guards** — `scripts/ci/check_release_ready.sh` and
   `scripts/ci/check_public_history_text.sh`. These catch the never-leak classes:
   private paths, personal email, credentials, ROM/media artifacts, and
   proprietary SDK-notice text, in the tree and in reachable history.
3. **Strict verify green** — a `docs/fidelity/reports/verify_<sha>.json` report
   for the exact `HEAD` commit, verdict `green`. Produce it with
   `GE007_VERIFY_STRICT=1 tools/fidelity/verify_all.sh`. A non-green verdict can
   only proceed with an explicit `--verify-report <path> --red-note "<why>"`
   owner adjudication, which is logged into the push annotation.
4. **Gameplay attestation (releases only)** — `--confirm-gameplay
   "macos=<initials/date>,windows=<initials/date>"`; owner gameplay verification
   on macOS **and** Windows. A `--dev-push` skips only this gate (never the
   others), for routine non-release `main` updates.
5. **Never force-push** — there is no force flag; a non-fast-forward push is
   refused (rebase onto the remote first).

Without `--yes` it is a non-destructive dry run that prints the exact rev range
that would be pushed. Examples:

```sh
# routine main update (guards + strict verify, no gameplay gate), preview then push:
scripts/publish_public.sh --dev-push
scripts/publish_public.sh --dev-push --yes

# release: tag locally, gameplay-verify on macOS + Windows, then publish the tag:
git tag -a v0.4.0 -m "MGB64 v0.4.0"
scripts/publish_public.sh --tag v0.4.0 \
  --confirm-gameplay "macos=AK/2026-07-11,windows=AK/2026-07-11" --yes
```

`scripts/release.sh --publish` (below) runs this guard chain itself and stops if
it goes red, so the GitHub Release artifacts can never be attached to a tree that
would fail the publish gate.

## The internal / external boundary (operationally)

- **Internal = the `export-ignore`'d trees.** `.gitattributes export-ignore` is
  the single source of truth for what is internal-only: today `docs/design/**`,
  `docs/fidelity/**`, `tools/fidelity/**`, `baselines/tapes/**`, and a few named
  path-gap docs. `export-ignore` keeps those out of `git archive` / GitHub
  "Download ZIP" and a fresh `create_public_launch_repo.sh` tree.
- **What the guards catch (the never-leak classes).** Because the day-to-day
  publish path is ordinary merged history (not an archive), `export-ignore` alone
  does not stop internal *content* reaching the remote. The publish gate's guards
  do: private paths, personal email, credentials, ROM/media, and proprietary
  SDK-notice text — scanned in the tree and in reachable history on every push.
- **Accepted-as-is, forward-only.** The history already on the public remote
  (including the fidelity trees and AI-authorship trailers) is accepted; there is
  no rewrite. The rule is forward: new pushes flow through the gate, which is why
  `git push origin main` is never run by hand.
- **Where the ruling lives.** The owner decision that set this boundary is the
  resolved **C1/D1** entry in `docs/fidelity/ESCALATIONS.md`.

## Cut a release

1. **Windows + Linux (CI):** a maintainer dispatches the release workflow
   (Actions → "Release build (Windows + Linux)" → Run, with a version label).
   It builds, packages, verifies asset-free, headless-smokes the Linux launcher,
   and uploads `mgb64-windows-*` / `mgb64-linux-*` artifacts. Download them into
   `dist/`.
2. **Push the release tag through the gate (git history):**
   ```sh
   git tag -a v0.3.0 -m "MGB64 v0.3.0"
   scripts/publish_public.sh --tag v0.3.0 \
     --confirm-gameplay "macos=<initials/date>,windows=<initials/date>" --yes
   ```
   This is the only path that puts the tag on the public remote (strict verify +
   gameplay gate). Do it **before** step 3 — `release.sh` will not mint the tag.

3. **macOS build + attach the release assets (local):**
   ```sh
   # build + validate macOS, stage all dist/ assets, attach them to the release:
   scripts/release.sh --version v0.3.0 --repo akratch/mgb64 \
     --confirm-gameplay "macos=<initials/date>,windows=<initials/date>" --publish
   # signed + notarized (credentials exported per "Code signing" above): add --sign
   # rolling 'latest' prerelease refreshed from main: add --rolling-latest
   ```
   Without `--publish` it only builds/stages `dist/` assets and prints the next
   command. `--publish` **requires `--confirm-gameplay`** (the gameplay gate is
   enforced here structurally, not as a printed reminder) and `gh` auth. It runs
   the publish guard chain, then attaches every `dist/mgb64-*-<version>.*` present
   (macOS locally + Windows/Linux from CI). For a **version** tag it uses
   `gh release create --verify-tag`, so it can **never mint the tag
   server-side** — the git tag must already be on the remote from step 2. Without
   `--sign`, the macOS asset ships ad-hoc signed and Gatekeeper will warn end
   users on first launch.

The README's **Download** table links to `/releases/latest`, so a rolling
`latest` prerelease keeps the download current between tagged majors.

---

## Verifying a download (users)

Every published release carries a commit-bound `mgb64-manifest-<version>.json`
and `mgb64-SHA256SUMS-<version>.txt`. Once releases are signed (see below),
both also ship a minisign signature (`.minisig`). To verify a download, put the
assets + manifest + SHA256SUMS + `.minisig` files in one folder and run:

```sh
# needs minisign (brew install minisign / apt install minisign)
scripts/release/verify_release.sh <download-folder> \
  -P scripts/release/mgb64-release-pubkey.txt      # or -P "RW...keystring"
```

It fails closed at every step: signature check on the manifest, signature check
on the SHA256SUMS, then a sha256 check of every listed asset, and finally
prints the bound source commit — compare it with the tag's target commit on
the release page. The release public key lives at
`scripts/release/mgb64-release-pubkey.txt`. **Note:** until the first signed
release that file is a marked placeholder and the verifier refuses to run
against it (there is nothing to verify yet — use the plain SHA256SUMS).

## First signed release (owner)

The manifest-signing key is minisign, minted and kept **only on the owner's
Mac** — deliberately no CI secret, no `id-token` permission change (same
posture as Apple signing above). One-time setup:

```sh
brew install minisign
minisign -G -p mgb64-release.pub -s ~/.minisign/mgb64-release.key
# choose a passphrase; the secret key never leaves this machine
```

Then:

1. Replace the placeholder `scripts/release/mgb64-release-pubkey.txt` with the
   generated `mgb64-release.pub` contents (commit it — it is public).
2. Export the key path (and optionally the passphrase for non-interactive
   signing; it is piped to minisign's stdin, never echoed):
   ```sh
   export MGB64_MANIFEST_SIGNING_KEY="$HOME/.minisign/mgb64-release.key"
   export MGB64_MANIFEST_PASSPHRASE="..."   # optional; else minisign prompts
   ```
3. Add `--sign-manifest` to the publish command:
   ```sh
   scripts/release.sh --version vX.Y.Z --repo akratch/mgb64 \
     --confirm-gameplay "macos=…,windows=…" --sign --sign-manifest --publish
   ```
   It fails fast (before the build) if `MGB64_MANIFEST_SIGNING_KEY` or the
   minisign binary is missing, signs `mgb64-manifest-<v>.json` and
   `mgb64-SHA256SUMS-<v>.txt` after the provenance gate passes, and attaches
   both `.minisig` files to the release. Without the flag the release is
   byte-identical to today's (unsigned) flow.

---

## The repository model (historical note)

> **Superseded — read `docs/WORKFLOW.md` for the current model.** The project used
> to carry two disjoint repos (a private `mgb64-prepublic-*` staging fork and the
> public `akratch/mgb64`). That two-repo model is **retired**: there is now a
> **single lineage** — `akratch/mgb64` is the one source of truth, developed in
> the open, and the old private repo is archived read-only. The paragraphs below
> are kept only for historical context; do not treat the private staging fork as
> a live development target.

- **`akratch/mgb64`** — the **public** repo. Canonical, developed **in the open**
  with real, additive history. Its root is a one-time clean snapshot (the private
  pre-history could not ship — it contained proprietary/SDK-notice text — so the
  public history begins at the launch snapshot and grows normally from there).
- **`akratch/mgb64-prepublic-*`** — the **archived** private staging repo
  (read-only). It held the full pre-launch development history (kept private for
  the reason above); it is no longer a development target.

`main` on the public repo is protected: **changes go through a PR** (0 required
approvals so a solo maintainer can self-merge; admins can merge). No direct
pushes, no force-pushes.

## Developing in the open (going forward)

This is now a normal open-source project — no more single-root re-squashes.

```sh
git clone git@github.com:akratch/mgb64.git   # work from the public repo
git checkout -b my-change
# ...edit, build (MGB64_APP=ON), test...
git push -u origin my-change
gh pr create --base main --fill                # open a PR
gh pr merge --rebase --delete-branch           # merge (self-merge is fine)
```

Outside contributors PR the same way; you review + merge theirs. Keep the tree
asset-free — `scripts/ci/check_release_ready.sh` is the guard, and `.gitattributes
export-ignore` keeps internal dev docs (`*_PLAN`/`*_GUIDE`/
`*_THEORY`/`*_REVIEW`/`*_WIP`) out of the public tree.

### The one-time launch snapshot (historical, done)

The v0.3.0 launch published a **single clean snapshot** of the sanitized tree as
one commit on top of the previous public root, via a PR (not a force-push). The
sanitized tree is produced by `scripts/create_public_launch_repo.sh` (applies the
`export-ignore` filter and re-runs every release guard); the maintainer gate is
`scripts/release_preflight.sh`. That path only matters if a future re-baseline is
ever needed — routine work is just PRs (above).

---

## Checklist

- [ ] Hooks installed (`scripts/install_git_hooks.sh`; `git config core.hooksPath` == `.githooks`).
- [ ] `scripts/release_preflight.sh` passes (clean tree, warning-clean build, ROM-free tests, asset-free).
- [ ] Strict **full-coverage** verify green for the release commit: `GE007_VERIFY_STRICT=1 tools/fidelity/verify_all.sh` (no `--tier`; the gate rejects a tier-limited report).
- [ ] PortMaster/GLES lane green (release CI "PortMaster GLES compile check", or `tools/portmaster_build_check.sh`).
- [ ] **`windows-validate.yml` dispatched and GREEN on the release commit.** This
      is the "Windows RUNS" execution gate (import-table guard + headless
      execution smoke + ROM-free CTest on a real windows-latest kernel). It is
      `workflow_dispatch`-only and does not auto-run — dispatch it against the
      release commit and confirm green *before* cutting.
- [ ] Windows + Linux CI artifacts downloaded into `dist/`.
- [ ] Owner gameplay verification done on **macOS AND Windows**.
- [ ] Public git tag pushed **first**, only via `scripts/publish_public.sh --tag <v> --confirm-gameplay "macos=…,windows=…" --yes`.
- [ ] `scripts/release.sh --version <v> --repo <r> --confirm-gameplay "macos=…,windows=…" --publish` (guard chain + gameplay gate run; macOS built locally; assets attached to the already-pushed tag via `--verify-tag`).
- [ ] `RELEASE_NOTES.md` updated, including WebGPU as the default renderer (+ fallback env vars) and the macOS unsigned right-click → Open note (Apple signing deferred).
- [ ] README Download links resolve (macOS asset present on `/releases/latest`).

### Owner-gated tail (first production release)

These are the irreducible owner/hardware/credential steps. Cross-reference the
authoritative handoff detail in `docs/audit/HARNESS_STRATEGY.md` §8 E5 rather than
re-deriving it; this list is the release-facing summary.

- [ ] **Owner gameplay attestation on macOS AND Windows** (`--confirm-gameplay`,
      structurally enforced by the publish gate — see "The publish gate" above).
      Also the deprecation-Phase-M precondition on macOS.
- [ ] **`windows-validate.yml` dispatched green** on the release commit (see the
      main checklist above; it had never been dispatched at audit time).
- [ ] **Manifest signing key (minisign).** `minisign -G` the long-term keypair on
      the owner's Mac; replace the marked placeholder
      `scripts/release/mgb64-release-pubkey.txt` with the real public key (commit
      it); run the first signed release with `--sign-manifest`. minisign 0.12 is
      installed; the placeholder pubkey + fail-closed `--sign-manifest` scaffold
      landed by design in the pipeline — the owner mints the real key at the first
      signed release. Unsigned releases ship fine until then (plain SHA256SUMS).
- [ ] **Disposable-repo publish test.** Before the first real publish, run the
      guarded publish path end-to-end against a throwaway repo to prove the
      pipeline (tag → guard chain → `gh release create --verify-tag` → asset
      attach + signing) — it has been statically validated only, never executed.
- [ ] **Enable GitHub Pages once**: Settings → Pages → Source →
      **GitHub Actions**. One-time repo setting; required before `web-demo.yml`
      can deploy anything.
- [ ] **Dispatch `web-demo.yml`** (`workflow_dispatch`-only, owner-triggered)
      and confirm the build + size-budget + ROM-absence guard jobs are green.
- [ ] **Verify the live Pages page on Chrome and Safari 26 with a real ROM**:
      pick the ROM, confirm it boots, play briefly, reload and confirm the
      stored ROM is picked back up, and confirm a save persists across a
      reload (see WEB.md's storage/quota notes for what "persists" means).
- [ ] **PortMaster hardware validation.** Real Mali/panfrost handheld WebGPU
      gameplay run (tape byte-exact + parity within tolerance). This is the
      "last real unknown" and **gates deprecation Phase G** (retiring the shared
      `gfx_opengl.c`/GLES path) — see `docs/BACKEND_DEPRECATION_PLAN.md`.
- [ ] **Apple signing/notarization decision.** Developer-ID `--sign` is on record
      as **deferred**; unsigned macOS ships with the right-click → Open note. Make
      (or re-affirm) the enrollment decision.
