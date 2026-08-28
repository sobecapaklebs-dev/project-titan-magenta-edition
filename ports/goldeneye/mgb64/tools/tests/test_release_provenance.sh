#!/usr/bin/env bash
#
# test_release_provenance.sh -- ROM-free unit tests for the AUDIT-0052 release
# provenance binding: scripts/release/stamp_provenance.sh (producer) and
# scripts/release/verify_provenance.sh (fail-closed verifier).
#
# Builds fixture dist/ trees entirely in a tempdir (no network, no ROM, no real
# binaries) and asserts the verifier accepts ONLY a fully commit-bound asset set
# and rejects every provenance defect: wrong sha, wrong commit, wrong version,
# modified file, missing sidecar, extra/renamed stale asset, orphan sidecar.
#
# Counts failures explicitly and exits nonzero on any (does NOT rely on assert;
# the ctest build is Release -DNDEBUG). Wired as ctest `release_provenance_guard`.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAMP="$ROOT/scripts/release/stamp_provenance.sh"
VERIFY="$ROOT/scripts/release/verify_provenance.sh"
for s in "$STAMP" "$VERIFY"; do [[ -f "$s" ]] || { echo "FATAL: not found: $s" >&2; exit 2; }; done

WORK="$(mktemp -d "${TMPDIR:-/tmp}/release_prov_test_XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf '  PASS %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  FAIL %s\n' "$1"; fail=$((fail+1)); }

VERSION="v9.9.9"
SHA="0123456789abcdef0123456789abcdef01234567"
OTHER_SHA="ffffffffffffffffffffffffffffffffffffffff"

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}';
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}';
  else echo ""; fi
}

# Build a clean, fully-stamped fixture dist bound to $SHA/$VERSION (emulating the
# CI producer: GITHUB_* env populated).
make_dist() {
  local d="$WORK/dist"; rm -rf "$d"; mkdir -p "$d"
  printf 'linux-binary-bytes\n'   > "$d/mgb64-linux-$VERSION.tar.gz"
  printf 'windows-binary-bytes\n' > "$d/mgb64-windows-$VERSION.zip"
  printf 'macos-binary-bytes\n'   > "$d/mgb64-macos-$VERSION.zip"
  local f
  for f in "$d"/mgb64-*-"$VERSION".*; do
    GITHUB_SHA="$SHA" GITHUB_ACTIONS=true GITHUB_RUN_ID=42 GITHUB_RUN_NUMBER=7 \
      GITHUB_WORKFLOW="Release build (Windows + Linux)" bash "$STAMP" "$f" "$VERSION" >/dev/null
  done
  echo "$d"
}

run_verify() {  # dist expected_commit -- extra args...
  local d="$1" c="$2"; shift 2
  bash "$VERIFY" --dist "$d" --version "$VERSION" --commit "$c" "$@"
}

expect_rc() {  # want label -- cmd...
  local want="$1" label="$2"; shift 2
  local rc; "$@" >/dev/null 2>&1 && rc=0 || rc=$?
  if [[ "$rc" -eq "$want" ]]; then ok "$label (exit $rc)"; else bad "$label -- expected $want, got $rc"; fi
}

echo "== release provenance: producer + fail-closed verifier =="

# 1. Happy path: correct set accepted, checksums + manifest emitted.
D="$(make_dist)"
CS="$D/mgb64-SHA256SUMS-$VERSION.txt"; MAN="$D/mgb64-manifest-$VERSION.json"
expect_rc 0 "fully commit-bound set accepted" run_verify "$D" "$SHA" --out-checksums "$CS" --out-manifest "$MAN"
[[ -f "$CS" ]]  && ok "SHA256SUMS emitted"  || bad "SHA256SUMS not emitted"
[[ -f "$MAN" ]] && ok "manifest emitted"    || bad "manifest not emitted"
if grep -q "$(sha256_of "$D/mgb64-linux-$VERSION.tar.gz")  mgb64-linux-$VERSION.tar.gz" "$CS"; then
  ok "checksums digest matches linux asset"; else bad "checksums digest wrong"; fi
if grep -q "\"commit\": \"$SHA\"" "$MAN" && [[ "$(grep -c '\"artifact\"' "$MAN")" -eq 3 ]]; then
  ok "manifest binds commit + all 3 assets"; else bad "manifest content wrong"; fi
expect_rc 0 "re-verify ignores generated manifest/checksums" run_verify "$D" "$SHA"

# 2. Wrong expected commit rejected.
D="$(make_dist)"; expect_rc 1 "wrong expected commit rejected" run_verify "$D" "$OTHER_SHA"

# 3. Sidecar version mismatch rejected (asset name right, stamp lies).
D="$(make_dist)"
sed -i.bak 's/"version": "'"$VERSION"'"/"version": "v0.0.0"/' "$D/mgb64-linux-$VERSION.tar.gz.provenance.json"
rm -f "$D"/*.bak
expect_rc 1 "sidecar version mismatch rejected" run_verify "$D" "$SHA"

# 4. Modified asset (content changed after stamping) rejected.
D="$(make_dist)"; printf 'tamper\n' >> "$D/mgb64-windows-$VERSION.zip"
expect_rc 1 "modified asset (sha mismatch) rejected" run_verify "$D" "$SHA"

# 5. Missing sidecar rejected.
D="$(make_dist)"; rm -f "$D/mgb64-macos-$VERSION.zip.provenance.json"
expect_rc 1 "asset with missing sidecar rejected" run_verify "$D" "$SHA"

# 6. Extra/renamed stale asset with no sidecar rejected.
D="$(make_dist)"; printf 'stale-substituted\n' > "$D/mgb64-linuxarm-$VERSION.tar.gz"
expect_rc 1 "extra/renamed unstamped asset rejected" run_verify "$D" "$SHA"

# 7. Orphan sidecar naming a missing asset rejected.
D="$(make_dist)"; cp "$D/mgb64-macos-$VERSION.zip.provenance.json" "$D/mgb64-ghost-$VERSION.zip.provenance.json"
expect_rc 1 "orphan sidecar (asset absent) rejected" run_verify "$D" "$SHA"

# 8. Producer stamp is well-formed + commit/sha/builder correct.
D="$(make_dist)"; side="$D/mgb64-linux-$VERSION.tar.gz.provenance.json"
if grep -q "\"commit\": \"$SHA\"" "$side" \
   && grep -q "\"sha256\": \"$(sha256_of "$D/mgb64-linux-$VERSION.tar.gz")\"" "$side" \
   && grep -q '\"builder\": \"github-actions\"' "$side"; then
  ok "producer sidecar records commit+sha+builder"; else bad "producer sidecar malformed"; fi

echo
echo "== release provenance: ${pass} passed, ${fail} failed =="
[[ "$fail" -eq 0 ]] || exit 1
