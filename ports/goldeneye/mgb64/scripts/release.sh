#!/usr/bin/env bash
#
# release.sh -- maintainer release helper.
#
# Builds + validates the macOS app LOCALLY (the platform this repo is developed
# on), assembles the release assets into dist/, and OPTIONALLY cuts/updates the
# GitHub Release. Windows + Linux binaries come from the release CI
# (.github/workflows/release.yml) — download those artifacts into dist/ first if
# you want them in the same release.
#
# Nothing here is destructive by default: without --publish it only builds local
# assets and prints the next command. See docs/RELEASING.md.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

version="dev"
publish=0
rolling=0
repo=""
skip_macos=0
universal=0
sign=0
sign_manifest=0
skip_notarize=0
gameplay=""

usage() {
  cat <<'USAGE'
Usage: scripts/release.sh [options]
  --version VER     Version label (e.g. v0.3.0). Default: dev
  --repo OWNER/NAME GitHub repo to publish to (required with --publish)
  --publish         Create/update the GitHub Release with the assets in dist/.
                    Requires --confirm-gameplay (owner ruling C1/D1) and NEVER
                    mints the tag server-side: for a version tag the git tag must
                    already be on the remote (pushed via
                    scripts/publish_public.sh --tag), enforced with gh's
                    --verify-tag.
  --confirm-gameplay "macos=<initials/date>,windows=<initials/date>"
                    Owner gameplay attestation on macOS AND Windows. Required
                    with --publish; same shape the publish gate enforces.
  --rolling-latest  Publish to a rolling 'latest' prerelease instead of a tag
  --skip-macos      Don't build macOS (just publish whatever is in dist/)
  --universal       Build a universal arm64+x86_64 macOS binary. Default: host
                    arch only. A universal build needs a universal SDL2; a plain
                    Homebrew SDL2 is single-arch and will fail the x86_64 link,
                    so the shipped prebuilt is Apple-Silicon-only (see README).
  --sign            Code-sign + notarize MGB64.app with a Developer ID cert
                    before zipping. Requires DEVELOPER_ID_APPLICATION,
                    APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD in the
                    environment (see docs/RELEASING.md).
  --skip-notarize   With --sign, sign only (skip the notarization submission).
                    Useful for a quick local check of the signing identity.
  --sign-manifest   (AUDIT-0052) minisign-sign the generated release manifest +
                    SHA256SUMS at publish time, producing the .minisig files
                    users verify with scripts/release/verify_release.sh.
                    Requires MGB64_MANIFEST_SIGNING_KEY (path to the minisign
                    secret key, which never leaves the owner's machine) and the
                    minisign binary; fails fast if either is missing. Optional
                    MGB64_MANIFEST_PASSPHRASE supplies the key passphrase
                    non-interactively. See docs/RELEASING.md "First signed
                    release". Without this flag nothing is signed (unchanged
                    behavior).
USAGE
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version="$2"; shift 2 ;;
    --repo) repo="$2"; shift 2 ;;
    --publish) publish=1; shift ;;
    --confirm-gameplay) gameplay="$2"; shift 2 ;;
    --rolling-latest) rolling=1; shift ;;
    --skip-macos) skip_macos=1; shift ;;
    --universal) universal=1; shift ;;
    --sign) sign=1; shift ;;
    --sign-manifest) sign_manifest=1; shift ;;
    --skip-notarize) skip_notarize=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 1 ;;
  esac
done

# Fail fast on missing signing credentials, before the (slow) build below.
if [[ "$sign" -eq 1 ]]; then
  : "${DEVELOPER_ID_APPLICATION:?--sign requires DEVELOPER_ID_APPLICATION (e.g. 'Developer ID Application: Name (TEAMID)')}"
  if [[ "$skip_notarize" -eq 0 ]]; then
    : "${APPLE_ID:?--sign requires APPLE_ID, or pass --skip-notarize}"
    : "${APPLE_TEAM_ID:?--sign requires APPLE_TEAM_ID, or pass --skip-notarize}"
    : "${APPLE_APP_PASSWORD:?--sign requires APPLE_APP_PASSWORD, or pass --skip-notarize}"
  fi
fi
# (AUDIT-0052 signing layer) Fail fast on missing manifest-signing prerequisites,
# before the (slow) build below. Without --sign-manifest this block is inert.
if [[ "$sign_manifest" -eq 1 ]]; then
  : "${MGB64_MANIFEST_SIGNING_KEY:?--sign-manifest requires MGB64_MANIFEST_SIGNING_KEY (path to the minisign secret key; see docs/RELEASING.md First signed release)}"
  if [[ ! -f "$MGB64_MANIFEST_SIGNING_KEY" ]]; then
    echo "ERROR: --sign-manifest: MGB64_MANIFEST_SIGNING_KEY names a missing file: $MGB64_MANIFEST_SIGNING_KEY" >&2
    exit 1
  fi
  command -v minisign >/dev/null 2>&1 || {
    echo "ERROR: --sign-manifest requires the minisign binary (brew install minisign)." >&2
    exit 1
  }
fi

dist="dist"; mkdir -p "$dist"

# 1. macOS app + .zip (built + validated on this machine).
if [[ "$skip_macos" -eq 0 ]]; then
  if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: macOS build must run on macOS (or pass --skip-macos)." >&2; exit 1
  fi
  if [[ "$universal" -eq 1 ]]; then
    echo "[release] building MGB64.app (universal arm64+x86_64)..."
    ./macos/Scripts/build_gl_app.sh --universal --output build-macos-app/MGB64.app
  else
    echo "[release] building MGB64.app (host arch: $(uname -m))..."
    ./macos/Scripts/build_gl_app.sh --output build-macos-app/MGB64.app
  fi
  ./macos/Scripts/verify_asset_free.sh build-macos-app/MGB64.app
  if [[ "$sign" -eq 1 ]]; then
    echo "[release] signing MGB64.app (Developer ID)..."
    sign_args=(build-macos-app/MGB64.app)
    [[ "$skip_notarize" -eq 1 ]] && sign_args+=(--skip-notarize)
    ./macos/Scripts/sign_and_notarize.sh "${sign_args[@]}"
  else
    echo "[release] --sign not passed: MGB64.app will ship ad-hoc signed (Gatekeeper will warn)."
  fi
  ( cd build-macos-app && ditto -c -k --sequesterRsrc --keepParent MGB64.app \
      "$OLDPWD/$dist/mgb64-macos-$version.zip" )
  # (AUDIT-0052) Bind the local macOS asset to this commit: write its provenance
  # stamp (commit == git HEAD, builder == local-darwin) so the publish-time
  # verify_provenance.sh can prove it was built from the verified source.
  "$(git rev-parse --show-toplevel)/scripts/release/stamp_provenance.sh" \
    "$dist/mgb64-macos-$version.zip" "$version"
  echo "[release] macOS asset: dist/mgb64-macos-$version.zip"
fi

echo "[release] assets staged in dist/:"
ls -1 "$dist"/mgb64-*-"$version".* 2>/dev/null || echo "  (none for version $version)"

# 2. Publish (explicit only).
if [[ "$publish" -eq 1 ]]; then
  [[ -n "$repo" ]] || { echo "ERROR: --publish requires --repo OWNER/NAME." >&2; exit 1; }
  command -v gh >/dev/null || { echo "ERROR: gh CLI required for --publish." >&2; exit 1; }

  # Compose (don't duplicate) the public-publish guard chain: run the ONE guarded
  # entrypoint's dry-run so clean-tree + release-ready + history-text + strict
  # verify are enforced before any release artifact is attached. STOP on red.
  # (dev-push dry-run skips only the gameplay gate, which is enforced separately
  # at the git-history push below.) See docs/RELEASING.md "The publish gate".
  echo "[release] running the public-publish guard chain (scripts/publish_public.sh --dev-push, dry-run)..."
  head_sha="$(git rev-parse --short=12 HEAD)"
  if ! "$(git rev-parse --show-toplevel)/scripts/publish_public.sh" --dev-push; then
    echo "ERROR: public-publish guards failed -- refusing to publish the release." >&2
    echo "       Fix the guard failures above (or produce a strict verify report for HEAD)." >&2
    exit 1
  fi

  # (C2) Owner gameplay gate -- STRUCTURAL, not a printed reminder. A publish with
  # no attestation, or a lazily-shaped one, is refused right here (same shape the
  # publish gate enforces: macos AND windows, each <initials>/<date>).
  [[ -n "$gameplay" ]] || {
    echo "ERROR: --publish requires --confirm-gameplay \"macos=<initials/date>,windows=<initials/date>\"" >&2
    echo "       (owner gameplay verification on macOS AND Windows -- ruling C1/D1)." >&2
    exit 1
  }
  gp_macos="" ; gp_windows=""
  IFS=',' read -ra _pairs <<< "$gameplay"
  for pair in "${_pairs[@]}"; do
    [[ "$pair" == *=* ]] || { echo "ERROR: --confirm-gameplay entry '$pair' is not key=value." >&2; exit 1; }
    k="${pair%%=*}"; v="${pair#*=}"; k="${k// /}"
    case "$k" in
      macos) gp_macos="$v" ;;
      windows) gp_windows="$v" ;;
      *) echo "ERROR: --confirm-gameplay unknown key '$k' (expected macos/windows)." >&2; exit 1 ;;
    esac
  done
  for pv in "macos:$gp_macos" "windows:$gp_windows"; do
    val="${pv#*:}"
    [[ -n "${val// /}" && "$val" == */* ]] || {
      echo "ERROR: --confirm-gameplay ${pv%%:*} value must be <initials>/<date> (got '${val}')." >&2; exit 1; }
  done
  echo "[release] gameplay attested: macos=${gp_macos} windows=${gp_windows}"
  cat >&2 <<GATE
[release] boundary reminders (ruling C1/D1, docs/fidelity/ESCALATIONS.md):
  - macOS ships UNSIGNED (Apple signing deferred): note the right-click > Open
    first-launch step in the release notes.
  - PortMaster/GLES lane must be green (release CI "PortMaster GLES compile check",
    or: tools/portmaster_build_check.sh).
GATE

  # (AUDIT-0052) Provenance binding -- STRUCTURAL, not a printed caveat. Every
  # dist/ asset must carry a provenance stamp (written at build time by
  # scripts/release/stamp_provenance.sh: the macOS step above, and the Linux/
  # Windows release-CI jobs) whose recorded sha256 matches the file on disk and
  # whose commit == this HEAD and version == this release. verify_provenance.sh
  # fails CLOSED on any missing, extra/renamed, orphan, modified, wrong-sha, or
  # wrong-version asset, and emits the SHA256SUMS + manifest.json that ship with
  # the release for user-side verification.
  full_sha="$(git rev-parse HEAD)"
  checksums="$dist/mgb64-SHA256SUMS-$version.txt"
  manifest="$dist/mgb64-manifest-$version.json"
  if ! "$(git rev-parse --show-toplevel)/scripts/release/verify_provenance.sh" \
        --dist "$dist" --version "$version" --commit "$full_sha" \
        --out-checksums "$checksums" --out-manifest "$manifest"; then
    echo "ERROR: release provenance verification failed (HEAD ${head_sha}) --" >&2
    echo "       refusing to publish. Rebuild every asset from THIS commit so each" >&2
    echo "       carries a matching provenance stamp (see the FAIL lines above)." >&2
    exit 1
  fi

  # (AUDIT-0052 signing layer, owner-gated) Sign the commit-bound manifest AND
  # the SHA256SUMS with the release minisign key so users can verify either one
  # (verify_release.sh checks both). The key lives only on the owner's machine
  # (that is why minisign, not CI-side attestation, was chosen). The .minisig
  # outputs match the publish glob below, so they ship with the release.
  if [[ "$sign_manifest" -eq 1 ]]; then
    echo "[release] signing manifest + SHA256SUMS (minisign)..."
    for f in "$manifest" "$checksums"; do
      rm -f "$f.minisig"
      if [[ -n "${MGB64_MANIFEST_PASSPHRASE:-}" ]]; then
        # Non-interactive: minisign reads the passphrase from stdin. The value is
        # piped, never echoed and never placed on a command line.
        printf '%s\n' "$MGB64_MANIFEST_PASSPHRASE" | \
          minisign -S -s "$MGB64_MANIFEST_SIGNING_KEY" \
            -t "mgb64 $version commit $full_sha" -m "$f"
      else
        minisign -S -s "$MGB64_MANIFEST_SIGNING_KEY" \
          -t "mgb64 $version commit $full_sha" -m "$f"
      fi
      [[ -f "$f.minisig" ]] || { echo "ERROR: minisign produced no signature for $f -- refusing to publish." >&2; exit 1; }
    done
    echo "[release] signed: $manifest.minisig + $checksums.minisig"
  fi

  # Publish the real binaries plus the generated SHA256SUMS + manifest.json, but
  # NOT the per-asset .provenance.json sidecars (AUDIT-0052): those are the
  # internal build inputs verify_provenance.sh consolidated into manifest.json,
  # so shipping the raw sidecars would just clutter the public release.
  mapfile -t assets < <(ls -1 "$dist"/mgb64-*-"$version".* 2>/dev/null | grep -v '\.provenance\.json$')
  [[ ${#assets[@]} -gt 0 ]] || { echo "ERROR: no dist/ assets for version $version." >&2; exit 1; }

  tag="$version"; extra=()
  if [[ "$rolling" -eq 1 ]]; then tag="latest"; extra=(--prerelease); fi
  notes_file="RELEASE_NOTES.md"; [[ -f "$notes_file" ]] || notes_file="/dev/null"

  echo "[release] publishing ${#assets[@]} asset(s) to $repo @ $tag ..."
  if gh release view "$tag" --repo "$repo" >/dev/null 2>&1; then
    gh release upload "$tag" "${assets[@]}" --repo "$repo" --clobber
  elif [[ "$rolling" -eq 1 ]]; then
    # Rolling 'latest' prerelease is a GitHub-managed rolling pointer, not a
    # version tag pushed through the git gate; create/refresh it directly.
    gh release create "$tag" "${assets[@]}" --repo "$repo" \
      --title "MGB64 $version" --notes-file "$notes_file" "${extra[@]}"
  else
    # (C2) A VERSION release: never let gh mint the tag server-side. --verify-tag
    # aborts unless the git tag already exists on the remote (it must have arrived
    # via scripts/publish_public.sh --tag, i.e. through the gameplay-gated git
    # push). This closes the "gh release create mints public tags outside the
    # gate" hole.
    gh release create "$tag" "${assets[@]}" --repo "$repo" --verify-tag \
      --title "MGB64 $version" --notes-file "$notes_file" "${extra[@]}" || {
        echo "ERROR: no git tag '$tag' on $repo -- refusing to mint it here." >&2
        echo "       Push it through the gate FIRST:" >&2
        echo "         scripts/publish_public.sh --tag $tag --confirm-gameplay \"$gameplay\" --yes" >&2
        echo "       then re-run this publish to attach the dist/ assets." >&2
        exit 1
      }
  fi
  echo "[release] done: https://github.com/$repo/releases/tag/$tag"
else
  echo "[release] not published. To publish:"
  echo "  scripts/release.sh --version $version --repo <owner/name> --publish"
fi
