#!/usr/bin/env bash
#
# test_publish_public.sh -- ROM-free unit tests for scripts/publish_public.sh.
#
# Exercises every refusal path plus the happy-path dry-run and a real push,
# entirely inside a throwaway scratch repo with a LOCAL BARE repo standing in
# for the public remote. It never touches origin or any network remote: the
# guards are stubbed via PUBLISH_RELEASE_READY_CMD / PUBLISH_HISTORY_TEXT_CMD,
# the verify-report dir via PUBLISH_REPORTS_DIR, and the only push target is the
# local bare repo created in a tempdir.
#
# Wired as the ROM-free ctest `publish_public_guard` (see CMakeLists.txt).
#
set -euo pipefail

SCRIPT_UNDER_TEST="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts/publish_public.sh"
[[ -f "$SCRIPT_UNDER_TEST" ]] || { echo "FATAL: not found: $SCRIPT_UNDER_TEST" >&2; exit 2; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/publish_public_test_XXXXXX")"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

pass=0; fail=0
ok()   { printf '  \033[32mPASS\033[0m %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  \033[31mFAIL\033[0m %s\n' "$1"; fail=$((fail+1)); }

# Isolate git identity/config from the host.
export HOME="$WORK/home"; mkdir -p "$HOME"
export GIT_CONFIG_GLOBAL="$WORK/home/.gitconfig"
export GIT_CONFIG_SYSTEM=/dev/null
git config --global user.email "test@example.invalid"
git config --global user.name "Publish Test"
git config --global init.defaultBranch main
git config --global commit.gpgsign false
git config --global advice.detachedHead false

REPO="$WORK/repo"
BARE="$WORK/public.git"

# Guard stubs.
STUB_PASS="$WORK/stub_pass.sh"; printf '#!/usr/bin/env bash\nexit 0\n' > "$STUB_PASS"; chmod +x "$STUB_PASS"
STUB_FAIL="$WORK/stub_fail.sh"; printf '#!/usr/bin/env bash\necho "stub guard: RED" >&2\nexit 1\n' > "$STUB_FAIL"; chmod +x "$STUB_FAIL"

REPORTS="$WORK/reports"

# Rebuild a clean scratch repo + fresh bare remote before each test group.
setup_repo() {
  rm -rf "$REPO" "$BARE" "$REPORTS"
  mkdir -p "$REPO" "$REPORTS"
  git init -q "$REPO"
  ( cd "$REPO"
    echo "hello" > file.txt
    git add file.txt
    git commit -qm "initial commit"
  )
  git init -q --bare "$BARE"
  write_green_report
}

sha12() { ( cd "$REPO" && git rev-parse --short=12 HEAD ); }

write_green_report() {
  local s; s="$(sha12)"
  cat > "$REPORTS/verify_${s}.json" <<EOF
{ "sha": "${s}", "started": "now", "gates": [], "verdict": "green", "fail_count": 0, "skip_count": 0, "strict": true, "tier_limit": 0 }
EOF
}

write_report_verdict() {  # verdict [sha-override]
  local s="${2:-$(sha12)}"
  cat > "$REPORTS/verify_$(sha12).json" <<EOF
{ "sha": "${s}", "started": "now", "gates": [], "verdict": "$1", "fail_count": 1, "skip_count": 0, "strict": true, "tier_limit": 0 }
EOF
}

# Green report with an arbitrary tier_limit (0 = full; N>0 = partial --tier run).
write_green_tier() {  # tier_limit
  local s; s="$(sha12)"
  cat > "$REPORTS/verify_${s}.json" <<EOF
{ "sha": "${s}", "started": "now", "gates": [], "verdict": "green", "fail_count": 0, "skip_count": 0, "strict": true, "tier_limit": $1 }
EOF
}

# Green report with NO sha field at all (forged/malformed).
write_green_no_sha() {
  cat > "$REPORTS/verify_$(sha12).json" <<EOF
{ "started": "now", "gates": [], "verdict": "green", "fail_count": 0, "skip_count": 0, "strict": true, "tier_limit": 0 }
EOF
}

# Run the script under test from inside the scratch repo with stub env.
run() {
  ( cd "$REPO"
    PUBLISH_RELEASE_READY_CMD="${RR:-$STUB_PASS}" \
    PUBLISH_HISTORY_TEXT_CMD="${HT:-$STUB_PASS}" \
    PUBLISH_REPORTS_DIR="$REPORTS" \
    bash "$SCRIPT_UNDER_TEST" "$@"
  )
}

# assert_exit CODE "label" -- runs with $ARGS, captures rc+output.
OUT=""
expect() {  # expected_code label -- args...
  local want="$1" label="$2"; shift 2
  local rc; OUT="$( run "$@" 2>&1 )" && rc=0 || rc=$?
  if [[ "$rc" -eq "$want" ]]; then ok "$label (exit $rc)"; else
    bad "$label -- expected exit $want, got $rc"; printf '    --- output ---\n%s\n    --------------\n' "$OUT" | sed 's/^/    /'
  fi
}
expect_grep() {  # needle label
  local needle="$1" label="$2"
  if grep -qF -- "$needle" <<<"$OUT"; then ok "$label"; else bad "$label -- output missing: $needle"; fi
}

GAMEPLAY='macos=AK/2026-07-11,windows=AK/2026-07-11'

echo "== publish_public.sh refusal + happy-path tests =="

# --- Argument / force refusals (no repo needed, but set one up anyway) --------
setup_repo
expect 2 "unknown arg -> usage"                 --bogus
expect 2 "--force refused"                       --force --yes --dev-push
expect 2 "--force-with-lease refused"            --force-with-lease --dev-push

# --- (a) dirty tree -----------------------------------------------------------
setup_repo
echo "dirty" >> "$REPO/file.txt"
expect 3 "dirty working tree refused"            --dev-push
expect_grep "working tree is dirty" "dirty tree message"

# --- (b) hygiene guards -------------------------------------------------------
setup_repo
RR="$STUB_FAIL" expect 4 "release-ready guard red refused"   --dev-push
setup_repo
HT="$STUB_FAIL" expect 4 "history-text guard red refused"    --dev-push

# --- (c) verify-report gate ---------------------------------------------------
setup_repo
rm -f "$REPORTS"/verify_*.json
expect 5 "missing verify report refused"         --dev-push
expect_grep "no strict verify report" "missing-report message"

setup_repo
write_report_verdict red
expect 5 "non-green verify (no override) refused" --dev-push

setup_repo
write_report_verdict red
expect 5 "non-green override without red-note refused" --dev-push --verify-report "$REPORTS/verify_$(sha12).json"

setup_repo
write_green_report
# stale green report: sha field does not match HEAD
write_report_verdict green "deadbeefdead"
expect 5 "stale green report (sha mismatch) refused" --dev-push

# --- (d) gameplay gate --------------------------------------------------------
setup_repo
expect 6 "release push without gameplay refused"
expect_grep "requires --confirm-gameplay" "gameplay-required message"

setup_repo
expect 6 "gameplay missing windows refused"      --confirm-gameplay "macos=AK/2026-07-11"
setup_repo
expect 6 "gameplay missing macos refused"        --confirm-gameplay "windows=AK/2026-07-11"
setup_repo
expect 6 "gameplay empty macos value refused"    --confirm-gameplay "macos= ,windows=AK/2026-07-11"

# (I5) attestation must be deliberately shaped, not the laziest string.
setup_repo
expect 6 "bare-key attestation (no '=') refused"     --confirm-gameplay "macos,windows"
setup_repo
expect 6 "attestation value == key refused"          --confirm-gameplay "macos=macos,windows=AK/2026-07-11"
setup_repo
expect 6 "attestation value w/o slash refused"       --confirm-gameplay "macos=AK-today,windows=AK/2026-07-11"
setup_repo
expect 6 "unknown attestation platform key refused"  --confirm-gameplay "mac=AK/2026-07-11,windows=AK/2026-07-11"

# --- happy-path dry runs ------------------------------------------------------
setup_repo
expect 0 "dev-push dry run OK (skips gameplay)"  --dev-push
expect_grep "gameplay gate skipped" "dev-push skip message"
expect_grep "DRY RUN" "dev-push dry-run banner"

setup_repo
expect 0 "release dry run OK (with gameplay)"    --confirm-gameplay "$GAMEPLAY"
expect_grep "PLANNED PUSH" "release dry-run planned-push line"
expect_grep "DRY RUN" "release dry-run banner"

# adjudicated non-green override, dev-push dry run
setup_repo
write_report_verdict red
expect 0 "non-green override w/ red-note OK"      --dev-push --verify-report "$REPORTS/verify_$(sha12).json" --red-note "known-flaky gate X, ticket #99"
expect_grep "ADJUDICATED" "adjudication logged to output"

# --- rev-range preview against a populated remote -----------------------------
setup_repo
# Seed the bare remote's main with HEAD, then advance HEAD by one commit so the
# preview shows exactly the one new commit (fast-forward).
( cd "$REPO"
  git remote add pub "$BARE"
  git push -q pub HEAD:refs/heads/main
  echo "second" > second.txt && git add second.txt && git commit -qm "second commit"
  git fetch -q pub )
write_green_report   # HEAD moved, need a fresh green report
expect 0 "fast-forward preview OK"               --remote pub --dev-push
expect_grep "second commit" "preview lists the new commit"
expect_grep "PLANNED PUSH" "preview planned-push line"

# --- (e) non-fast-forward refusal ---------------------------------------------
setup_repo
( cd "$REPO"
  git remote add pub "$BARE"
  # Remote main gets a commit that HEAD does not contain.
  git push -q pub HEAD:refs/heads/main
  tmpc="$(git rev-parse HEAD)"
  git checkout -q -b _remote_only
  echo "remoteonly" > r.txt && git add r.txt && git commit -qm "remote-only commit"
  git push -q pub HEAD:refs/heads/main
  git checkout -q main
  git fetch -q pub )
write_green_report
expect 7 "non-fast-forward push refused"         --remote pub --dev-push
expect_grep "non-fast-forward" "non-ff message"

# --- (f) real push to the bare fake remote (--yes) ----------------------------
setup_repo
( cd "$REPO" && git remote add pub "$BARE" )
expect 0 "real push to fake remote (--yes)"      --remote pub --confirm-gameplay "$GAMEPLAY" --yes
remote_head="$( git --git-dir="$BARE" rev-parse main 2>/dev/null || echo MISSING )"
local_head="$( cd "$REPO" && git rev-parse HEAD )"
if [[ "$remote_head" == "$local_head" ]]; then ok "fake remote main advanced to HEAD"; else bad "fake remote main != HEAD ($remote_head vs $local_head)"; fi
if [[ -f "$REPORTS/publish_annotations.log" ]] && grep -q '"verify_verdict": "green"' "$REPORTS/publish_annotations.log"; then
  ok "push annotation recorded"
else bad "push annotation not recorded"; fi

# idempotent: second push finds nothing to do
( cd "$REPO" && git fetch -q pub )
expect 0 "second push is a no-op"                --remote pub --confirm-gameplay "$GAMEPLAY" --yes
expect_grep "nothing to push" "no-op message"

# --- (C1) a TAG must still push when the branch is already current ------------
# The original bug: with main already at HEAD, --tag exit-0'd "nothing to push"
# BEFORE the tag push, silently dropping the release tag.
setup_repo
( cd "$REPO"
  git remote add pub "$BARE"
  git push -q pub HEAD:refs/heads/main         # remote main == HEAD (current)
  git tag -a v9.9.9 -m "release 9.9.9"         # local annotated tag at HEAD
  git fetch -q pub )                            # pub/main tracking ref == HEAD
write_green_report
expect 0 "tag pushes even when branch current"  --remote pub --tag v9.9.9 --confirm-gameplay "$GAMEPLAY" --yes
expect_grep "branch is current" "tag-only proceed message"
if [[ -n "$( git --git-dir="$BARE" tag -l v9.9.9 2>/dev/null )" ]]; then
  ok "tag v9.9.9 reached the fake remote (C1 fixed)"
else bad "tag v9.9.9 did NOT reach the fake remote (C1 regression)"; fi

# --- (I1) a green report with no sha field is refused (fail closed) -----------
setup_repo
rm -f "$REPORTS"/verify_*.json
write_green_no_sha
expect 5 "sha-less green report refused"          --dev-push
expect_grep "sha == HEAD" "sha-less refusal message"

# --- (I2) a tier-limited green report is refused; full-coverage passes --------
setup_repo
write_green_tier 1
expect 5 "tier-limited (tier 1) verify report refused" --dev-push
expect_grep "tier-limited" "tier-limited refusal message"
setup_repo
write_green_tier 0
expect 0 "full-coverage (tier 0) verify report OK"     --dev-push

# --- (I3) adjudication-note injection is neutralized -------------------------
setup_repo
( cd "$REPO" && git remote add pub "$BARE" )
write_report_verdict red
INJ='pwned""" , "forged": true, "x": "\ end'
expect 0 "adjudicated push w/ injecty red-note (--yes)" \
  --remote pub --dev-push --verify-report "$REPORTS/verify_$(sha12).json" --red-note "$INJ" --yes
if python3 - "$REPORTS/publish_annotations.log" "$INJ" <<'PY'
import json, sys
recs = [json.loads(l) for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
pub = [r for r in recs if r.get("event") == "publish"][-1]
ok = (pub.get("red_note") == sys.argv[2]) and ("forged" not in pub)
sys.exit(0 if ok else 1)
PY
then ok "red-note injection neutralized (note intact verbatim, no injected keys)"
else bad "red-note injection NOT neutralized"; fi

# --- (I4) test seams are refused when the remote resolves to the public repo --
setup_repo
( cd "$REPO" && git remote add pub "$BARE" )
MGB64_PUBLIC_REMOTE_RE='public\.git$' \
  expect 2 "test seams refused on a public-resolved remote"  --remote pub --dev-push
expect_grep "must not be set when the remote resolves to the public repo" "seams-on-public message"

# (I4/M3) annotation records the seam-override + completion marker on a real push
setup_repo
( cd "$REPO" && git remote add pub "$BARE" )
expect 0 "real push records seam + push_ok"       --remote pub --confirm-gameplay "$GAMEPLAY" --yes
if grep -q '"test_seams_overridden": true' "$REPORTS/publish_annotations.log"; then
  ok "annotation records the seam override"; else bad "annotation missing seam-override flag"; fi
if grep -q '"event": "push_ok"' "$REPORTS/publish_annotations.log"; then
  ok "annotation records the post-push completion marker"; else bad "annotation missing push_ok marker"; fi

# --- (M1) refuse pushing a HEAD that isn't the named branch -------------------
setup_repo
( cd "$REPO" && git checkout -q -b topic/x )
expect 2 "branch/HEAD mismatch refused"           --dev-push
expect_grep "mismatched HEAD" "branch-mismatch message"
expect 0 "branch mismatch overridable"            --dev-push --branch topic/x

echo
echo "== publish_public.sh: ${pass} passed, ${fail} failed =="
[[ "$fail" -eq 0 ]] || exit 1
