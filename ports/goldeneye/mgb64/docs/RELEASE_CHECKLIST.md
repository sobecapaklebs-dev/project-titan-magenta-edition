# Maintainer Release Checklist

Use this before tagging a source release, attaching any build artifact, or making
a broad public announcement. The goal is simple: no bundled game content, clear
third-party provenance, and status claims that match what the tree actually does.

## Preferred Preflight

For a maintainer release pass with a local ROM available, run the preflight from
a clean checkout and keep the ROM outside the repository:

```sh
scripts/release_preflight.sh \
  --deep-runtime \
  --rom /path/outside/repo/baserom.u.z64 \
  --macos-app-bundle-sdl2 \
  --strict-ignored \
  --github
```

Omit `--macos-app-bundle-sdl2` on Linux or Windows; the app-bundle lane is
macOS-only. Add `--macos-app-strict-deployment-target` only when `pkg-config`
points at a controlled SDL2 build with the intended minimum macOS version.

The preflight covers the core source hygiene guard, ROM-backed validation when a
ROM is supplied, ignored-artifact checks, source-archive smoke testing, and
GitHub public-surface checks.

## Source Hygiene

At minimum, a release candidate should pass:

```sh
./scripts/ci/check_release_ready.sh
./scripts/ci/check_no_rom_data.sh
./scripts/ci/check_high_risk_ignored_artifacts.sh
./tools/validate_quick.sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4 2>&1 | tee /tmp/mgb64-build.log
python3 tools/summarize_build_warnings.py \
  /tmp/mgb64-build.log \
  --json-out /tmp/mgb64-build-warnings.json
ctest --test-dir build --output-on-failure
git status --short
```

`git status --short` should show no tracked changes. Also review
`git clean -ndX` before packaging from a local checkout so ignored ROMs,
extracted assets, screenshots, audio dumps, saves, and build output cannot be
swept into an archive by mistake.

`src/bootcode.s` must remain a public placeholder: no IPL3 boot ROM code,
boot-font byte block, old SM64/n64split provenance text, or locally supplied
matching-target boot material should be present in tracked source.

Review `/tmp/mgb64-build-warnings.json` before posting release notes. Treat
compiler/linker warnings as release blockers unless a known warning threshold is
deliberately documented with an issue and rationale.

## ROM-Backed Runtime Checks

When a locally owned ROM and native binary are available, include the deeper
runtime lanes:

```sh
./tools/route_contract_smoke.sh --native-smoke --all --no-build --rom /path/outside/repo/baserom.u.z64 --binary build/ge007
./tools/spawn_health_check.sh --all --no-build --rom /path/outside/repo/baserom.u.z64 --binary build/ge007
./tools/playability_smoke.sh --all --no-build --rom /path/outside/repo/baserom.u.z64 --binary build/ge007
./tools/renderer_parity_capture.sh --no-build --rom /path/outside/repo/baserom.u.z64 --binary build/ge007
./tools/save_persistence_check.sh --no-build --rom /path/outside/repo/baserom.u.z64 --binary build/ge007
```

After a deep-runtime run, review the structured summaries under the generated
`/tmp/mgb64_*` output directories. Traces, screenshots, emulator logs, saves,
and JSON summaries can be ROM-derived local evidence; keep them out of git and
do not redistribute them.

For audio changes, capture at least one deterministic trace and confirm the
final PCM rail-hit count is not unexpectedly nonzero:

```sh
mkdir -p /tmp/ge007_audio_savedir
GE007_AUDIO_TRACE=/tmp/ge007_audio.jsonl \
  GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  ./build/ge007 --rom /path/outside/repo/baserom.u.z64 --level 33 --deterministic \
  --savedir /tmp/ge007_audio_savedir \
  --screenshot-frame 120 --screenshot-exit
```

For music-fidelity changes, compare against an emulator or hardware reference
capture when one is available:

```sh
tools/prepare_ares_audio_dump_build.sh

tools/ares_startup_audio_reference.sh \
  --ares-bin build/ares-audio-dump/ares/build-audio-dump/desktop-ui/ares.app/Contents/MacOS/ares \
  --rom /path/outside/repo/baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --seconds 110 \
  --rate 22050 \
  --dump-frames 1984500

tools/startup_music_reference_check.sh \
  --no-build \
  --rom /path/outside/repo/baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --frames 2700 \
  --reference /tmp/mgb64_audio_ref/ares_boot_22050.raw \
  --reference-format raw \
  --reference-raw-rate 22050 \
  --min-compared-seconds 80 \
  --report-only
```

Treat this as a spectral/envelope/stereo regression lane. It catches thin,
overly bright, missing-frequency, gain-staging, and L/R-bus mistakes without
claiming bit-perfect N64 audio.

## Source Archives

Use the repository archive helper for source artifacts:

```sh
scripts/make_public_source_archive.sh --force
scripts/smoke_public_source_archive.sh \
  dist/mgb64-$(git rev-parse --short=12 HEAD).tar.gz \
  --max-warnings 0
```

The archive helper uses `git archive`, so only tracked files are packaged. The
smoke test proves the user-facing tarball configures, builds, and passes the
ROM-free CTest suite without relying on `.git` or ignored local files.

## macOS App Checks

The current app path is an unsigned local source build. Before attaching or
announcing any macOS app artifact, verify the unsigned bundle is asset-free:

```sh
./macos/Scripts/build_universal.sh --release --build-dir build-macos-universal
./macos/Scripts/verify_asset_free.sh build-macos-universal/libge007_lib.a
./macos/Scripts/build_app_bundle.sh --release \
  --build-dir build-macos-app \
  --output build-macos-app/MGB64.app
./macos/Scripts/verify_asset_free.sh build-macos-app/libge007_lib.a
./macos/Scripts/verify_asset_free.sh build-macos-app/MGB64.app
```

For a redistributable app candidate, rebuild with a controlled SDL2 prefix and
fail closed on the deployment target before signing:

```sh
PKG_CONFIG_PATH=/path/to/controlled-sdl2/lib/pkgconfig \
  ./macos/Scripts/build_app_bundle.sh --release \
    --build-dir build-macos-app \
    --output build-macos-app/MGB64.app \
    --deployment-target 13.0 \
    --strict-deployment-target \
    --bundle-sdl2
```

For `.app` inputs, `verify_asset_free.sh` checks both the executable and the
bundle resource allowlist, so accidental ROMs, captures, audio dumps, or
unexpected media resources fail the gate.

## Public Claims

- Say clearly that users must provide their own legally dumped ROM.
- Say clearly that no ROM, extracted assets, or ROM media are distributed.
- Do not describe the repository as fully clean-room while the SDK/libultra
  compatibility material listed in [../THIRD_PARTY.md](../THIRD_PARTY.md)
  remains in-tree.
- Avoid "from-scratch decompilation" shorthand while the matching-target
  SDK/libultra provenance exception remains in-tree.
- Keep direct SDK/devkit source-path or copied-documentation comments out of
  public source. Keep provenance in [../THIRD_PARTY.md](../THIRD_PARTY.md) and
  use project-owned source comments.
- Keep [PROVENANCE_AUDIT.md](PROVENANCE_AUDIT.md) aligned when touching
  native-compiled SDK-shaped support code.
- Do not claim a DMG, signed binary, notarized binary, or redistributable macOS
  release until that path is wired and verified. The current `.app` path is an
  unsigned local source build.
- Do not add a Homebrew Cask, appcast URL, release-download URL, or package
  SHA placeholder until a real asset-free signed/notarized artifact exists.
- Keep [../PORT.md](../PORT.md) and
  [../RELEASE_NOTES.md](../RELEASE_NOTES.md) aligned before posting.

## GitHub Surface

- Keep Discussions, issue templates, PR templates, security reporting, and
  contributor labels enabled.
- Keep hosted GitHub Actions disabled unless the project deliberately changes to
  hosted CI. If Actions are enabled later, workflow tokens should remain
  read-only, external actions should stay pinned to full commit SHAs, and
  artifact/log retention should stay short.
- Protect `main` with review and conversation-resolution requirements, and keep
  force pushes/deletions disabled.
- Run the public GitHub surface checker after changing repository settings,
  labels, issues, releases, workflow configuration, or branch protection:

```sh
scripts/check_github_launch_ready.sh --repo akratch/mgb64
```

Despite the historical script name, this is now the public-repo hygiene check:
it verifies repository settings, branch/tag/PR ref reachability, workflow run
history, GitHub text surfaces, release assets, Actions artifacts, labels, and
security/protection settings.

Do not attach prebuilt binaries that bundle ROM-derived data. Native binaries
must remain asset-free and should pass `macos/Scripts/verify_asset_free.sh`
where applicable.

## Known Release Caveat

The largest remaining release-hygiene caveat is SDK/libultra-lineage
provenance. The current tree inventories that material and the release guard
fails if proprietary SDK notice text is reintroduced, but the most conservative
public branch should continue replacing or isolating matching-target-only SDK
compatibility source and auditing native-compiled SDK-shaped support code.
