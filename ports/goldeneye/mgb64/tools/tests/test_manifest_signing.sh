#!/usr/bin/env bash
#
# test_manifest_signing.sh -- ROM-free unit tests for the AUDIT-0052 manifest
# signing layer: the user-side verifier scripts/release/verify_release.sh and
# the fail-fast release.sh --sign-manifest scaffold.
#
# Uses an EPHEMERAL minisign keypair generated in a tempdir (minisign -G -W,
# no passphrase) that lives and dies with the test -- no long-term key material
# is ever created or committed. Skips cleanly if minisign is not installed
# (owner/maintainer tool, not a build dependency).
#
# Cases:
#   (a) ephemeral-signed manifest + SHA256SUMS accepted by verify_release.sh
#   (b) tampered manifest rejected (bad signature)
#   (c) tampered SHA256SUMS rejected (bad signature)
#   (d) tampered asset rejected (signed hash mismatch)
#   (e) committed PLACEHOLDER public key refused with an actionable message
#   (f) missing minisign binary (restricted PATH) fails closed with a clear message
#   (g) release.sh --sign-manifest without MGB64_MANIFEST_SIGNING_KEY fails fast
#       BEFORE any build work
#
# Counts failures explicitly and exits nonzero on any (does NOT rely on assert;
# the ctest build is Release -DNDEBUG). Wired as ctest `manifest_signing_guard`.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERIFY="$ROOT/scripts/release/verify_release.sh"
RELEASE="$ROOT/scripts/release.sh"
PLACEHOLDER_PUB="$ROOT/scripts/release/mgb64-release-pubkey.txt"
for s in "$VERIFY" "$RELEASE" "$PLACEHOLDER_PUB"; do
  [[ -f "$s" ]] || { echo "FATAL: not found: $s" >&2; exit 2; }
done

if ! command -v minisign >/dev/null 2>&1; then
  echo "SKIP: minisign not installed (brew install minisign) -- signing layer untestable here."
  exit 0
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/manifest_sign_test_XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf '  PASS %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  FAIL %s\n' "$1"; fail=$((fail+1)); }

VERSION="v9.9.9"
SHA="0123456789abcdef0123456789abcdef01234567"

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}';
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}';
  else echo ""; fi
}

# --- Ephemeral keypair (tempdir-only; no passphrase via -W; dies with $WORK) ---
KEYDIR="$WORK/keys"; mkdir -p "$KEYDIR"
minisign -G -W -p "$KEYDIR/ephemeral.pub" -s "$KEYDIR/ephemeral.key" >/dev/null 2>&1 \
  || { echo "FATAL: ephemeral minisign -G failed." >&2; exit 2; }
PUBFILE="$KEYDIR/ephemeral.pub"
PUBSTR="$(tail -n1 "$PUBFILE")"

# Build a signed release fixture: one asset + SHA256SUMS + manifest, both signed.
make_release() {
  local d="$WORK/rel"; rm -rf "$d"; mkdir -p "$d"
  printf 'linux-binary-bytes\n' > "$d/mgb64-linux-$VERSION.tar.gz"
  local digest; digest="$(sha256_of "$d/mgb64-linux-$VERSION.tar.gz")"
  printf '%s  %s\n' "$digest" "mgb64-linux-$VERSION.tar.gz" > "$d/mgb64-SHA256SUMS-$VERSION.txt"
  cat > "$d/mgb64-manifest-$VERSION.json" <<EOF
{
  "schema": "mgb64-release-manifest/1",
  "version": "$VERSION",
  "commit": "$SHA",
  "assets": [
    {"artifact": "mgb64-linux-$VERSION.tar.gz", "sha256": "$digest",
     "version": "$VERSION", "commit": "$SHA"}
  ]
}
EOF
  minisign -S -s "$KEYDIR/ephemeral.key" -m "$d/mgb64-manifest-$VERSION.json" >/dev/null 2>&1
  minisign -S -s "$KEYDIR/ephemeral.key" -m "$d/mgb64-SHA256SUMS-$VERSION.txt" >/dev/null 2>&1
  echo "$d"
}

expect_rc() {  # want label -- cmd...
  local want="$1" label="$2"; shift 2
  local rc; "$@" >/dev/null 2>&1 && rc=0 || rc=$?
  if [[ "$rc" -eq "$want" ]]; then ok "$label (exit $rc)"; else bad "$label -- expected $want, got $rc"; fi
}

expect_fail_msg() {  # label needle -- cmd...   (nonzero exit AND message present)
  local label="$1" needle="$2"; shift 2
  local out rc
  out="$("$@" 2>&1)" && rc=0 || rc=$?
  if [[ "$rc" -ne 0 ]] && grep -qF "$needle" <<< "$out"; then
    ok "$label (exit $rc, message present)"
  else
    bad "$label -- rc=$rc, message '$needle' $(grep -qF "$needle" <<< "$out" && echo found || echo MISSING)"
  fi
}

echo "== manifest signing: verify_release.sh + release.sh --sign-manifest =="

# (a) Happy path: ephemeral-signed release accepted (pubkey as file AND string).
D="$(make_release)"
expect_rc 0 "(a) signed release accepted (pubkey file)"   bash "$VERIFY" "$D" -P "$PUBFILE"
expect_rc 0 "(a) signed release accepted (pubkey string)" bash "$VERIFY" "$D" -P "$PUBSTR"
a_out="$(bash "$VERIFY" "$D" -P "$PUBFILE" 2>/dev/null)" || a_out=""
if grep -qF "commit  = $SHA" <<< "$a_out"; then
  ok "(a) bound commit surfaced to the user"; else bad "(a) bound commit not printed"; fi

# (b) Tampered manifest -> signature verification must reject.
D="$(make_release)"
sed -i.bak "s/$SHA/ffffffffffffffffffffffffffffffffffffffff/" "$D/mgb64-manifest-$VERSION.json"; rm -f "$D"/*.bak
expect_fail_msg "(b) tampered manifest rejected" "SIGNATURE INVALID" bash "$VERIFY" "$D" -P "$PUBFILE"

# (c) Tampered SHA256SUMS -> signature verification must reject.
D="$(make_release)"
printf 'deadbeef  mgb64-extra-file.zip\n' >> "$D/mgb64-SHA256SUMS-$VERSION.txt"
expect_fail_msg "(c) tampered SHA256SUMS rejected" "SIGNATURE INVALID" bash "$VERIFY" "$D" -P "$PUBFILE"

# (d) Tampered asset (signatures intact, hash mismatch) -> digest check rejects.
D="$(make_release)"
printf 'tamper\n' >> "$D/mgb64-linux-$VERSION.tar.gz"
expect_fail_msg "(d) tampered asset rejected" "ASSET DIGEST MISMATCH" bash "$VERIFY" "$D" -P "$PUBFILE"

# (e) Committed placeholder pubkey refused, with the actionable message.
D="$(make_release)"
expect_fail_msg "(e) placeholder pubkey refused" "PLACEHOLDER" bash "$VERIFY" "$D" -P "$PLACEHOLDER_PUB"
expect_rc 4 "(e) placeholder refusal uses its own exit code" bash "$VERIFY" "$D" -P "$PLACEHOLDER_PUB"

# (f) minisign missing (restricted PATH) -> fail closed with a clear message.
D="$(make_release)"
expect_fail_msg "(f) missing minisign fails closed" "minisign is not installed" \
  env PATH=/usr/bin:/bin bash "$VERIFY" "$D" -P "$PUBFILE"

# (g) release.sh --sign-manifest without MGB64_MANIFEST_SIGNING_KEY fails fast
#     BEFORE any build (--skip-macos + no --publish would otherwise be a cheap
#     no-op that exits 0, so a nonzero exit here proves the fail-fast fired).
expect_fail_msg "(g) --sign-manifest without key fails fast" "MGB64_MANIFEST_SIGNING_KEY" \
  env -u MGB64_MANIFEST_SIGNING_KEY bash "$RELEASE" --version "$VERSION" --skip-macos --sign-manifest
# (g2) ...and a key path that does not exist is also refused before any work.
expect_fail_msg "(g) --sign-manifest with missing key file fails fast" "MGB64_MANIFEST_SIGNING_KEY" \
  env MGB64_MANIFEST_SIGNING_KEY="$WORK/no-such.key" bash "$RELEASE" --version "$VERSION" --skip-macos --sign-manifest
# (g3) control: the same invocation WITHOUT the flag stays a successful no-op
#      (inertness: absent flag -> zero behavior change).
expect_rc 0 "(g) same invocation without --sign-manifest unaffected" \
  env -u MGB64_MANIFEST_SIGNING_KEY bash "$RELEASE" --version "$VERSION" --skip-macos

echo
echo "== manifest signing: ${pass} passed, ${fail} failed =="
[[ "$fail" -eq 0 ]] || exit 1
