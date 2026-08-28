#!/usr/bin/env bash
#
# publish_public.sh -- THE one guarded entrypoint for any push to the public
# remote (akratch/mgb64). Nothing else pushes public: not a raw `git push
# origin main`, not release.sh, not CI.
#
# Why this exists (owner ruling, 2026-07-11, docs/fidelity/ESCALATIONS.md,
# resolved C1/D1): the already-public history is accepted as-is; the boundary is
# FORWARD-ONLY and MECHANIZED. `.gitattributes export-ignore` is the single
# source of truth for internal trees; every public push flows through this
# script, which composes the release-ready + public-history-text guards, a
# strict verify-report gate, and (for releases) an owner gameplay attestation on
# macOS AND Windows. It NEVER rewrites history and NEVER filters content -- it
# accepts the tree as-is and lets the guards catch the never-leak classes
# (private paths, personal email, credentials, ROM/media, proprietary notices).
#
# It NEVER force-pushes. Without --yes it is a non-destructive dry run that
# prints exactly what would be pushed.
#
# See docs/RELEASING.md ("The publish gate") and docs/WORKFLOW.md for the
# operational doctrine.
#
# Usage:
#   scripts/publish_public.sh [options]
#
# Modes:
#   (default)            RELEASE push -- requires --confirm-gameplay (macOS AND
#                        Windows owner gameplay verification).
#   --dev-push           Non-release push to main. Skips ONLY the gameplay gate;
#                        every other guard (clean tree, release-ready,
#                        history-text, strict verify green) still runs.
#
# Push target:
#   --remote NAME        Remote to push to. Default: origin.
#   --branch NAME        Branch to update on the remote. Default: main.
#   --tag NAME           Also push this EXISTING local tag (implies RELEASE).
#
# Verify gate:
#   (automatic)          Requires docs/fidelity/reports/verify_<sha>.json for the
#                        current HEAD, verdict "green".
#   --verify-report PATH Adjudicate with a specific report instead of the
#                        auto-located one. If its verdict is not "green",
#                        --red-note is REQUIRED and is logged into the push
#                        annotation.
#   --red-note "TEXT"    Owner adjudication note for a non-green verify override.
#
# Gameplay gate (RELEASE only):
#   --confirm-gameplay "macos=<initials/date>,windows=<initials/date>"
#
# Execution:
#   --yes                Actually perform the push. Without it, dry-run only.
#   -h | --help          This help.
#
# Test seams (do not use for real publishes; refused when the resolved remote URL
# is the real public repo unless PUBLISH_ALLOW_SEAMS_ON_PUBLIC=1):
#   PUBLISH_RELEASE_READY_CMD   Override the release-ready guard command.
#   PUBLISH_HISTORY_TEXT_CMD    Override the public-history-text guard command.
#   PUBLISH_LEAK_SCAN_CMD       Override the ranged leak-class scan command.
#   PUBLISH_REPORTS_DIR         Override the verify-report / annotation directory.
#   MGB64_PUBLIC_REMOTE_RE      Extra regex that marks a remote URL/name public.
#
# Exit codes:
#   0  success (dry run OK, or push completed)
#   2  usage / bad arguments
#   3  dirty working tree
#   4  a hygiene guard failed (release-ready / history-text / leak-class scan)
#   5  verify-report missing or not green (and not adjudicated)
#   6  gameplay attestation missing or malformed
#   7  push would be non-fast-forward (would need a force -- refused)
#
set -euo pipefail

prog="publish_public"
die() { printf '%s: ERROR: %s\n' "$prog" "$1" >&2; exit "${2:-1}"; }
info() { printf '%s: %s\n' "$prog" "$1"; }

# Print the comment header (everything from line 2 up to the first non-# line), so
# the help text never silently truncates when the header grows (M4).
usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "${BASH_SOURCE[0]}"; }

# --- Args --------------------------------------------------------------------
mode="release"          # release | dev
remote="origin"
branch="main"
tag=""
verify_report=""
red_note=""
gameplay=""
do_push=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dev-push)          mode="dev"; shift ;;
    --remote)            remote="${2:?--remote needs a value}"; shift 2 ;;
    --branch)            branch="${2:?--branch needs a value}"; shift 2 ;;
    --tag)               tag="${2:?--tag needs a value}"; shift 2 ;;
    --verify-report)     verify_report="${2:?--verify-report needs a path}"; shift 2 ;;
    --red-note)          red_note="${2:?--red-note needs text}"; shift 2 ;;
    --confirm-gameplay)  gameplay="${2:?--confirm-gameplay needs a value}"; shift 2 ;;
    --yes)               do_push=1; shift ;;
    -h|--help)           usage; exit 0 ;;
    # Force pushing is categorically refused -- there is no flag for it.
    --force|-f|--force-with-lease|--force-with-lease=*|--mirror)
      die "force pushing is never allowed through this path (ruling: forward-only, no rewrite)" 2 ;;
    *) die "unknown argument: $1 (see --help)" 2 ;;
  esac
done

# A tag push is by definition a release.
[[ -n "$tag" ]] && mode="release"

# --- Repo context ------------------------------------------------------------
# Resolve helper scripts by THIS script's location (so the guard/scan commands are
# found even when the tests run publish_public.sh against a throwaway scratch repo
# whose toplevel has no scripts/ tree).
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git worktree" 2
cd "$root"

sha_full="$(git rev-parse HEAD)"
sha="$(git rev-parse --short=12 HEAD)"   # matches tools/fidelity/verify_all.sh keying
reports_dir="${PUBLISH_REPORTS_DIR:-$root/docs/fidelity/reports}"

release_ready_cmd="${PUBLISH_RELEASE_READY_CMD:-$script_dir/ci/check_release_ready.sh}"
history_text_cmd="${PUBLISH_HISTORY_TEXT_CMD:-$script_dir/ci/check_public_history_text.sh}"
leak_scan_cmd="${PUBLISH_LEAK_SCAN_CMD:-$script_dir/ci/scan_leak_classes.sh}"

# --- (I4) Remote-URL pin + test-seam discipline ------------------------------
# Confirm whether the resolved remote URL is the real public repo. Test seams
# (PUBLISH_*_CMD / PUBLISH_REPORTS_DIR) neuter the guard chain -- they must never
# be honored on a push whose remote actually resolves to akratch/mgb64.
resolved_url="$(git remote get-url "$remote" 2>/dev/null || echo "")"
is_public_url=0
case "$resolved_url" in
  *github.com[:/]akratch/mgb64|*github.com[:/]akratch/mgb64.git|*github.com[:/]akratch/mgb64/*) is_public_url=1 ;;
esac
if [[ -n "${MGB64_PUBLIC_REMOTE_RE:-}" ]] \
   && printf '%s\n%s\n' "$remote" "$resolved_url" | grep -qE "$MGB64_PUBLIC_REMOTE_RE"; then
  is_public_url=1
fi
seams_overridden=0
if [[ -n "${PUBLISH_RELEASE_READY_CMD:-}${PUBLISH_HISTORY_TEXT_CMD:-}${PUBLISH_LEAK_SCAN_CMD:-}${PUBLISH_REPORTS_DIR:-}" ]]; then
  seams_overridden=1
fi
if [[ "$is_public_url" -eq 1 && "$seams_overridden" -eq 1 && "${PUBLISH_ALLOW_SEAMS_ON_PUBLIC:-0}" != "1" ]]; then
  die "test seams (PUBLISH_*_CMD / PUBLISH_REPORTS_DIR) must not be set when the remote resolves to the public repo (${resolved_url})" 2
fi

# --- (M1) Refuse pushing a HEAD that is not the branch you named -------------
# Publishing the checked-out topic HEAD to public 'main' is almost always a
# mistake (WORKFLOW rule 2). Refuse a mismatch unless deliberately overridden.
cur_branch="$(git symbolic-ref --short -q HEAD || echo 'DETACHED')"
if [[ "$cur_branch" != "$branch" && "${PUBLISH_ALLOW_BRANCH_MISMATCH:-0}" != "1" ]]; then
  die "checked-out ref is '${cur_branch}' but --branch is '${branch}': refusing to push a mismatched HEAD.
       Check out ${branch} (or pass the intended --branch); override with PUBLISH_ALLOW_BRANCH_MISMATCH=1." 2
fi

info "HEAD ${sha} (${sha_full})"
info "mode=${mode} target=${remote}/${branch}${tag:+ tag=${tag}}${resolved_url:+ url=${resolved_url}}"

# --- (a) Refuse a dirty tree -------------------------------------------------
# Publishing an untracked/modified tree means the push would not match what the
# guards and verify report were computed against.
if [[ -n "$(git status --porcelain)" ]]; then
  git status --short >&2
  die "working tree is dirty -- commit or stash before publishing" 3
fi

# --- (b) Hygiene guards (both modes; never skipped) --------------------------
info "running release-ready guard: ${release_ready_cmd}"
if ! bash "$release_ready_cmd"; then
  die "release-ready guard failed -- refusing to publish" 4
fi
info "running public-history-text guard: ${history_text_cmd}"
if ! bash "$history_text_cmd"; then
  die "public-history-text guard failed -- refusing to publish" 4
fi

# --- (c) Strict verify-report gate -------------------------------------------
# Default: docs/fidelity/reports/verify_<sha>.json must exist and be "green".
# Override: --verify-report PATH; if that report is not green, --red-note is
# mandatory and is recorded in the push annotation.
default_report="${reports_dir}/verify_${sha}.json"
report_path="${verify_report:-$default_report}"

if [[ ! -f "$report_path" ]]; then
  if [[ -n "$verify_report" ]]; then
    die "verify report not found: ${report_path}" 5
  fi
  die "no strict verify report for HEAD ${sha}: expected ${default_report}
       run 'GE007_VERIFY_STRICT=1 tools/fidelity/verify_all.sh' at this commit,
       or adjudicate with --verify-report <path> --red-note \"...\"" 5
fi

# Parse verdict + embedded sha + tier coverage with python3 (the repo's JSON tool).
read -r report_verdict report_sha report_tier < <(python3 - "$report_path" <<'PY'
import json, sys
try:
    with open(sys.argv[1], encoding="utf-8") as fh:
        d = json.load(fh)
    print(d.get("verdict", "MISSING"), d.get("sha", "MISSING"), d.get("tier_limit", "MISSING"))
except Exception as e:
    print("PARSE_ERROR", "PARSE_ERROR", "PARSE_ERROR")
PY
)
[[ "$report_verdict" == "PARSE_ERROR" ]] && die "verify report is not valid JSON: ${report_path}" 5
info "verify report: ${report_path} verdict=${report_verdict} sha=${report_sha} tier_limit=${report_tier}"

adjudicated=0
if [[ "$report_verdict" == "green" ]]; then
  # (I1) A green report must carry sha == HEAD. A missing sha field is a hard
  # refusal, not a free pass -- verify_all.sh always writes sha, so a sha-less
  # report is malformed/forged and must never be accepted for any HEAD.
  if [[ "$report_sha" != "$sha" ]]; then
    die "verify report sha (${report_sha}) does not match HEAD (${sha}) -- stale or sha-less report (a green report must carry sha == HEAD)" 5
  fi
  # (I2) A tier-limited run (verify_all.sh --tier N) writes verdict green over a
  # fraction of the ratchet. A release gate needs full coverage: tier_limit must
  # be 0 (= all tiers). Missing field fails closed.
  if [[ "$report_tier" != "0" ]]; then
    die "verify report is tier-limited (tier_limit=${report_tier}) -- a release gate needs FULL-coverage verify;
       run 'GE007_VERIFY_STRICT=1 tools/fidelity/verify_all.sh' with no --tier at this commit" 5
  fi
else
  # Non-green: only a deliberate --verify-report adjudication with a written
  # red-note may proceed. The note is logged into the push annotation.
  if [[ -z "$verify_report" ]]; then
    die "verify verdict is '${report_verdict}', not green -- fix verify, or adjudicate
       with --verify-report ${report_path} --red-note \"why this is safe to ship\"" 5
  fi
  [[ -n "$red_note" ]] || die "non-green verify override requires --red-note \"...\"" 5
  adjudicated=1
  info "ADJUDICATED non-green verify (${report_verdict}): ${red_note}"
fi

# --- (d) Gameplay attestation (RELEASE only) ---------------------------------
if [[ "$mode" == "release" ]]; then
  [[ -n "$gameplay" ]] || die "release push requires --confirm-gameplay \"macos=<initials/date>,windows=<initials/date>\"
       (owner gameplay verification on macOS AND Windows -- ruling C1/D1)" 6
  # (I5) Require both platforms present with a DELIBERATELY-SHAPED attestation:
  # each entry must be key=value (contain '='), the value must be non-empty and
  # take the documented <initials>/<date> form (contain a '/'), and unknown keys
  # are rejected rather than silently ignored. This defeats the lazy
  # "macos,windows" (no '=') and "macos=macos" (value == key) inputs.
  gp_macos="" ; gp_windows=""
  IFS=',' read -ra _pairs <<< "$gameplay"
  for pair in "${_pairs[@]}"; do
    [[ "$pair" == *=* ]] || die "--confirm-gameplay entry '${pair}' is not key=value (need e.g. macos=AK/2026-07-11)" 6
    key="${pair%%=*}"; val="${pair#*=}"
    key="${key// /}"
    case "$key" in
      macos)   gp_macos="$val" ;;
      windows) gp_windows="$val" ;;
      *)       die "--confirm-gameplay: unknown platform key '${key}' (expected macos and windows)" 6 ;;
    esac
  done
  _attest_shaped() { [[ -n "${1// /}" && "$1" == */* ]]; }   # non-empty and <initials>/<date>-shaped
  _attest_shaped "$gp_macos"   || die "--confirm-gameplay macos value must be <initials>/<date> (got '${gp_macos}')" 6
  _attest_shaped "$gp_windows" || die "--confirm-gameplay windows value must be <initials>/<date> (got '${gp_windows}')" 6
  info "gameplay attested: macos=${gp_macos} windows=${gp_windows}"
else
  info "dev push: gameplay gate skipped (guards + strict verify still enforced)"
fi

# --- (e/f) Compute + print the exact rev range; refuse non-fast-forward -------
# NOTE (M2): this reads the local remote-tracking ref without fetching (by
# design -- this path never contacts the network on its own). git's own
# server-side non-ff refusal is the real backstop; the preview may be stale.
remote_ref="refs/remotes/${remote}/${branch}"
range_desc=""
branch_push=1          # (C1) may be flipped to 0 when the branch is already current
scan_commits=""        # the exact NEW commits this push introduces (for the leak scan)
if git rev-parse --verify --quiet "$remote_ref" >/dev/null; then
  base="$(git rev-parse "$remote_ref")"
  if [[ "$base" == "$sha_full" ]]; then
    # (C1) Branch already current. A branch-only push has nothing to do -- but a
    # tag push MUST still proceed (previously this exit 0 silently dropped the
    # tag, no-op'ing the release and steering operators into release.sh's
    # gh-release-create fallback that mints public tags outside the gate).
    if [[ -z "$tag" ]]; then
      info "nothing to push: ${remote}/${branch} already at ${sha}"
      exit 0
    fi
    branch_push=0
    range_desc="${remote}/${branch} already at ${sha} (branch push skipped) -- tag only"
    info "${remote}/${branch} already at ${sha}: branch is current; proceeding with tag ${tag} only"
  elif ! git merge-base --is-ancestor "$base" HEAD; then
    die "push would be non-fast-forward (${remote}/${branch} has commits not in HEAD).
       This path never force-pushes; rebase onto ${remote}/${branch} first." 7
  else
    range_desc="${remote}/${branch} (${base:0:12}) .. HEAD (${sha})"
    scan_commits="$(git rev-list "${base}..HEAD")"
    echo
    info "commits that would be pushed to ${remote}/${branch}:"
    git --no-pager log --oneline "${base}..HEAD"
  fi
else
  range_desc="new branch ${remote}/${branch} <- HEAD (${sha})"
  scan_commits="$(git rev-list HEAD --not --remotes="$remote" 2>/dev/null || git rev-list -n 50 HEAD)"
  echo
  info "no local remote-tracking ref for ${remote}/${branch}."
  info "  ('git fetch ${remote}' to preview the exact delta; git push still refuses non-fast-forward)"
  info "recent commits at HEAD:"
  git --no-pager log --oneline -n 20 HEAD
fi
[[ -n "$tag" ]] && { git rev-parse --verify --quiet "refs/tags/${tag}" >/dev/null || die "tag not found locally: ${tag} (create it with 'git tag -a ${tag}' first)" 2; }

# --- Belt-and-suspenders: ranged leak-class scan of the pushed commits --------
# The same scan the pre-push hook runs, over the exact NEW commits (content +
# messages). check_public_history_text.sh above is a --all superset scan; this is
# the ranged mirror so both entrypoints stay in lockstep.
if [[ -n "$scan_commits" ]]; then
  info "running ranged leak-class scan over the commits to be pushed"
  # shellcheck disable=SC2086  # SHAs are word-safe; intentional splitting.
  if ! bash "$leak_scan_cmd" $scan_commits; then
    die "leak-class scan failed -- refusing to publish" 4
  fi
fi

echo
info "PLANNED PUSH: ${range_desc}${tag:+  + tag ${tag}}"
if [[ "$branch_push" -eq 1 ]]; then
  info "  git push ${remote} HEAD:refs/heads/${branch}${tag:+  &&  git push ${remote} refs/tags/${tag}}"
else
  info "  git push ${remote} refs/tags/${tag}"
fi

# --- Execute (only with --yes) -----------------------------------------------
if [[ "$do_push" -ne 1 ]]; then
  echo
  info "DRY RUN -- nothing pushed. Re-run with --yes to publish."
  exit 0
fi

# Record the push annotation (durable, local, internal -- reports_dir is
# gitignored + export-ignored). The red-note adjudication lands here.
#
# (I3) All user-supplied strings (red_note, gameplay) and every other field are
# passed to python via the ENVIRONMENT, not interpolated into the source. A
# red-note containing `"""`, a backslash, or a newline can no longer break out of
# the literal to corrupt or truncate the durable adjudication record.
# (I4/M3) The record also captures the resolved remote URL and whether any test
# seam was overridden, so a seam-neutered push is never indistinguishable from a
# fully guarded one.
mkdir -p "$reports_dir"
annotation_log="${reports_dir}/publish_annotations.log"
PA_TS="$(date +%Y-%m-%dT%H:%M:%S%z)" \
PA_SHA="$sha" PA_SHA_FULL="$sha_full" PA_MODE="$mode" \
PA_REMOTE="$remote" PA_REMOTE_URL="$resolved_url" PA_BRANCH="$branch" PA_TAG="$tag" \
PA_BRANCH_PUSH="$branch_push" \
PA_REPORT="$(basename "$report_path")" PA_VERDICT="$report_verdict" \
PA_TIER="$report_tier" PA_ADJUDICATED="$adjudicated" \
PA_RED_NOTE="$red_note" PA_GAMEPLAY="$gameplay" \
PA_SEAMS="$seams_overridden" PA_IS_PUBLIC="$is_public_url" \
python3 - "$annotation_log" <<'PY'
import json, os, sys
rec = {
    "ts": os.environ["PA_TS"],
    "event": "publish",
    "sha": os.environ["PA_SHA"],
    "sha_full": os.environ["PA_SHA_FULL"],
    "mode": os.environ["PA_MODE"],
    "remote": os.environ["PA_REMOTE"],
    "remote_url": os.environ["PA_REMOTE_URL"],
    "remote_is_public": bool(int(os.environ["PA_IS_PUBLIC"])),
    "branch": os.environ["PA_BRANCH"],
    "branch_pushed": bool(int(os.environ["PA_BRANCH_PUSH"])),
    "tag": os.environ["PA_TAG"],
    "verify_report": os.environ["PA_REPORT"],
    "verify_verdict": os.environ["PA_VERDICT"],
    "verify_tier_limit": os.environ["PA_TIER"],
    "verify_adjudicated": bool(int(os.environ["PA_ADJUDICATED"])),
    "test_seams_overridden": bool(int(os.environ["PA_SEAMS"])),
    "red_note": os.environ["PA_RED_NOTE"],
    "gameplay": os.environ["PA_GAMEPLAY"],
}
with open(sys.argv[1], "a", encoding="utf-8") as fh:
    fh.write(json.dumps(rec) + "\n")
print("annotation ->", sys.argv[1])
PY

# (C3) Signal the pre-push hook that this push came through the guarded
# entrypoint, so its main/tag routing nudge is satisfied. The hook's leak-class
# scan runs regardless of this marker.
export MGB64_PUBLISH_GATE=1

echo
if [[ "$branch_push" -eq 1 ]]; then
  info "pushing HEAD -> ${remote}/${branch} (no force)"
  git push "$remote" "HEAD:refs/heads/${branch}"
else
  info "branch ${remote}/${branch} already current -- pushing tag only"
fi
if [[ -n "$tag" ]]; then
  info "pushing tag ${tag} -> ${remote} (no force)"
  git push "$remote" "refs/tags/${tag}"
fi

# (M3) Record a completion marker AFTER the push(es) succeed, so a durable
# "publish" record with no matching "push_ok" flags a push that aborted mid-way.
PA_TS="$(date +%Y-%m-%dT%H:%M:%S%z)" PA_SHA="$sha" PA_TAG="$tag" \
python3 - "$annotation_log" <<'PY'
import json, os, sys
with open(sys.argv[1], "a", encoding="utf-8") as fh:
    fh.write(json.dumps({
        "ts": os.environ["PA_TS"], "event": "push_ok",
        "sha": os.environ["PA_SHA"], "tag": os.environ["PA_TAG"],
    }) + "\n")
PY

echo
info "published. annotation recorded in ${annotation_log}"
