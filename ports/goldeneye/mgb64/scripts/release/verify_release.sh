#!/usr/bin/env bash
#
# verify_release.sh -- user-side release verifier (AUDIT-0052 signing layer).
#
# Cryptographically verifies a downloaded MGB64 release against the project's
# minisign public key, then checks every asset digest. Fails CLOSED at every
# step: a bad signature, a tampered manifest/checksums/asset, a missing signature
# or a missing tool all stop with a nonzero exit and a clear message.
#
# Usage:
#   scripts/release/verify_release.sh <dir> -P <pubkey-file-or-string>
#   scripts/release/verify_release.sh <manifest.json> <SHA256SUMS.txt> -P <pubkey>
#
# Steps (each fatal on failure):
#   1. minisign -V the manifest  (mgb64-manifest-*.json)  against its .minisig
#   2. minisign -V the checksums (mgb64-SHA256SUMS-*.txt)  against its .minisig
#   3. sha256 -c the checksums so every listed asset digest matches on disk
#   4. print the manifest's bound source commit for the user to compare with the
#      tag on the release page
#
# The public key ships at scripts/release/mgb64-release-pubkey.txt. Until the
# owner mints the real release key it is a PLACEHOLDER and this verifier refuses
# to run against it (fail-closed even there).
#
# Portable on purpose (bash 3.2, macOS/Linux; sha256sum OR shasum): a user runs
# it on a plain machine with only minisign added.
set -euo pipefail

me="$(basename "$0")"

usage() {
  cat <<'USAGE'
Usage: verify_release.sh <dir-or-files> -P <pubkey-file-or-string>

Verify a downloaded MGB64 release against the project's minisign public key.
  <dir>          a folder holding the downloaded release: the assets, the
                 manifest (mgb64-manifest-*.json), the SHA256SUMS
                 (mgb64-SHA256SUMS-*.txt) and their .minisig signatures.
  -P <pubkey>    the release public key: a path to a minisign public-key file,
                 or the base64 key string itself. The key ships at
                 scripts/release/mgb64-release-pubkey.txt.

Fails closed at every step. On success prints VERIFIED and the bound commit.
USAGE
}

die() { printf '%s: %s\n' "$me" "$1" >&2; exit "${2:-1}"; }

pubkey=""
positional=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -P|--pubkey) pubkey="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    --) shift; while [[ $# -gt 0 ]]; do positional+=("$1"); shift; done ;;
    -*) die "unknown option: $1 (see --help)" 2 ;;
    *) positional+=("$1"); shift ;;
  esac
done

[[ -n "$pubkey" ]] || die "a release public key is required: -P <pubkey-file-or-string>" 2
[[ ${#positional[@]} -gt 0 ]] || die "give the release directory (or the manifest + SHA256SUMS files)." 2

# 0a. minisign must be installed to check any signature -- fail closed if not.
command -v minisign >/dev/null 2>&1 || die \
  "minisign is not installed. Install it (e.g. 'brew install minisign', or your package manager) then re-run." 3

# 0b. Refuse the committed PLACEHOLDER key -- fail closed even here.
ms_pub=()
if [[ -f "$pubkey" ]]; then
  if grep -q 'MGB64-PUBKEY-PLACEHOLDER' "$pubkey"; then
    printf '%s: the public key at %s is the committed PLACEHOLDER, not a real release key.\n' "$me" "$pubkey" >&2
    printf '  No signed release has been minted yet, so there is nothing to verify against.\n' >&2
    printf '  Pass the real minisign public key published with the release, e.g.:\n' >&2
    printf '    %s <dir> -P RW...<the published key string>\n' "$me" >&2
    printf '  See docs/RELEASING.md -> "Verifying a download (users)".\n' >&2
    exit 4
  fi
  ms_pub=(-p "$pubkey")
else
  case "$pubkey" in
    *PLACEHOLDER*) die "refusing to verify against a PLACEHOLDER public key string (no real key yet)." 4 ;;
  esac
  ms_pub=(-P "$pubkey")
fi

# 1. Locate the manifest + SHA256SUMS (explicit files or discovered in a dir).
manifest=""; checksums=""; base_dir="."
if [[ ${#positional[@]} -eq 1 && -d "${positional[0]}" ]]; then
  base_dir="${positional[0]}"
else
  for p in "${positional[@]}"; do
    if [[ -d "$p" ]]; then base_dir="$p"; continue; fi
    [[ -f "$p" ]] || die "no such file: $p"
    case "$(basename "$p")" in
      *manifest*.json) manifest="$p"; base_dir="$(dirname "$p")" ;;
      *SHA256SUMS*)    checksums="$p"; base_dir="$(dirname "$p")" ;;
    esac
  done
fi

find_one() {  # base_dir glob [glob...] -> first existing match on stdout
  local d="$1"; shift
  local g matches
  for g in "$@"; do
    # shellcheck disable=SC2206  # deliberate glob expansion into an array
    matches=( "$d"/$g )
    if [[ -e "${matches[0]}" ]]; then printf '%s\n' "${matches[0]}"; return 0; fi
  done
  return 1
}

[[ -n "$manifest" ]]  || manifest="$(find_one "$base_dir" 'mgb64-manifest-*.json' 'manifest.json' '*manifest*.json' || true)"
[[ -n "$checksums" ]] || checksums="$(find_one "$base_dir" 'mgb64-SHA256SUMS-*.txt' 'SHA256SUMS' 'SHA256SUMS.txt' '*SHA256SUMS*' || true)"

[[ -n "$manifest" && -f "$manifest" ]]   || die "no release manifest (mgb64-manifest-*.json) found in '$base_dir'."
[[ -n "$checksums" && -f "$checksums" ]] || die "no SHA256SUMS (mgb64-SHA256SUMS-*.txt) found in '$base_dir'."
[[ -f "$manifest.minisig" ]]  || die "missing signature: $(basename "$manifest").minisig (unsigned or incomplete download)."
[[ -f "$checksums.minisig" ]] || die "missing signature: $(basename "$checksums").minisig (unsigned or incomplete download)."

echo "$me: manifest  = $manifest"
echo "$me: checksums = $checksums"

# 2 + 3. Verify both signatures against the release public key.
echo "$me: verifying signatures with minisign..."
minisign -V "${ms_pub[@]}" -m "$manifest" \
  || die "SIGNATURE INVALID for $(basename "$manifest") -- it does not match the release public key. DO NOT TRUST THIS DOWNLOAD."
minisign -V "${ms_pub[@]}" -m "$checksums" \
  || die "SIGNATURE INVALID for $(basename "$checksums") -- it does not match the release public key. DO NOT TRUST THIS DOWNLOAD."

# 4. Check every listed asset digest against the file on disk (in the asset dir).
echo "$me: checking asset digests (sha256)..."
sha_name="$(basename "$checksums")"
digest_ok=0
if command -v sha256sum >/dev/null 2>&1; then
  ( cd "$base_dir" && sha256sum -c "$sha_name" ) && digest_ok=1
elif command -v shasum >/dev/null 2>&1; then
  ( cd "$base_dir" && shasum -a 256 -c "$sha_name" ) && digest_ok=1
else
  die "no sha256 tool (sha256sum/shasum) available to check asset digests." 3
fi
[[ "$digest_ok" -eq 1 ]] \
  || die "ASSET DIGEST MISMATCH -- a file listed in $sha_name does not match its signed hash. DO NOT TRUST THIS DOWNLOAD."

# 5. Surface the bound source commit + version for the user to compare.
commit="$(grep -o '"commit"[[:space:]]*:[[:space:]]*"[^"]*"' "$manifest" | head -n1 | sed -E 's/.*:[[:space:]]*"([^"]*)".*/\1/')"
version="$(grep -o '"version"[[:space:]]*:[[:space:]]*"[^"]*"' "$manifest" | head -n1 | sed -E 's/.*:[[:space:]]*"([^"]*)".*/\1/')"

echo
echo "$me: VERIFIED -- signatures valid and every listed asset digest matches."
echo "$me:   version = ${version:-<unknown>}"
echo "$me:   commit  = ${commit:-<unknown>}"
echo "$me: Compare the commit above with the tag's target on the release page."
