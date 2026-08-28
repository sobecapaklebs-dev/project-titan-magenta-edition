#!/usr/bin/env bash
#
# check_public_history_text.sh -- scan reachable git history for high-risk text.
#
# The current tree can be clean while older reachable commits still contain
# private paths, handoff notes, credentials, or proprietary notice text. Public
# launch exposes branch history, so this guard checks every reachable commit.
#
# Scope is intentionally diff/blob CONTENT only (via `git log -G<pattern>`),
# never commit MESSAGES. That matters because this repo's actual publish path
# is docs/WORKFLOW.md's single-lineage model: ordinary commits reach the
# public remote by a plain `git push` of a merged PR branch, not by rewriting
# or regenerating history. Under that model, ~86+ published commits carry
# `Co-Authored-By: Claude*` / `Claude-Session:` trailers in their messages.
# D1 (2026-07-10 fidelity review, docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md)
# decided those trailers are accepted-published, not a defect to remediate --
# no history rewrite is planned or wanted to strip them. Do not "improve" this
# guard to grep commit messages/trailers: that would flag every one of those
# already-public commits and manufacture pressure for a history rewrite that
# was explicitly ruled out. This guard's job is narrower and stays that way:
# catch the private-path / secret / proprietary-notice classes below, in the
# text that actually entered a tracked file's content.
#
# Also note what this guard is NOT a substitute for: export-ignore (and the
# filtering scripts/create_public_launch_repo.sh performs) only ever applies
# to `git archive`/GitHub "Download ZIP" and to a fresh create_public_launch_repo.sh
# / prepare_public_launch_bundle.sh re-launch. It does nothing for the
# WORKFLOW.md day-to-day path (`git push origin/main` of a merged branch) --
# so the only real control for internal-only content (docs/design/**,
# docs/fidelity/**, tools/fidelity/**, baselines/tapes/**) is discipline: never
# land it on a branch that merges to main. export-ignore, this guard, and the
# release-ready guard (scripts/ci/check_release_ready.sh) are backstops for the
# archive/re-launch path and a text scan of history, not a replacement for that
# discipline.
#
# Scan scope truth (2026-07-10, CR-2 fix): being export-ignored from the
# archive does NOT exempt a path from this guard's history scan. The only
# paths this guard skips are the narrow, explicitly-marked "history-scan-exempt"
# set in .gitattributes (docs/design/** + the LEGACY dead paths) -- see the
# path-scope block below. Everything else that is merely export-ignored --
# today that's docs/fidelity/**, tools/fidelity/**, baselines/tapes/**,
# tools/tests/test_fidelity_ledger.py, and the path-gap docs -- is STILL
# content-scanned here, on purpose, because it still reaches the public remote
# verbatim via the direct-push path above.
#
set -euo pipefail

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "SKIP: not a git checkout; no reachable history to scan."
  exit 0
fi

cd "$(git rev-parse --show-toplevel)"

private_text='(/Users/[^[:space:]]+|Desktop/dev|/home/adam|github\.com/akratch/007|private dev repo|contaminated private|Claude|session handoff|agent memory|must never go public)'
secret_text='(AKIA[0-9A-Z]{16}|ghp_[0-9A-Za-z_]{36,}|github_pat_[0-9A-Za-z_]+|xox[baprs]-[0-9A-Za-z-]+|sk-[A-Za-z0-9]{20,}|BEGIN (RSA|OPENSSH|EC|DSA) PRIVATE KEY)'
sdk_notice_text='(UNPUBLISHED[[:space:]]+PROPRI''ETARY|may not be disclo''sed|without the prior written (permission|cons''ent)|RESTRICTED[[:space:]]+RIG''HTS|subparagraph \(c\)\(1\)\(ii\) of the Rig''hts)'
pattern="(${private_text}|${secret_text}|${sdk_notice_text})"

# Path scope. The three guard scripts embed the detection patterns themselves, so
# they are excluded.
#
# A narrow, explicitly-marked set of internal documentation is excluded too:
# ONLY the export-ignore entries between the ".gitattributes" comment markers
# "--- BEGIN history-scan-exempt ---" and "--- END history-scan-exempt ---"
# (today: docs/design/** and the LEGACY dead-path globs). This is NOT derived
# from every export-ignore entry in .gitattributes, and that is deliberate
# (2026-07-10, CR-2 fix): export-ignore alone only ever governs the `git
# archive` / GitHub "Download ZIP" artifact and a fresh
# scripts/create_public_launch_repo.sh checkout. It says nothing about whether
# a path's *history* is safe to stop scanning, because this repo's real
# publish path is a direct `git push` of ordinary merged history
# (docs/WORKFLOW.md rule 6) -- so trees that are export-ignored from the
# archive can still land, verbatim, on the public remote as ordinary commits.
# Concretely: this guard DOES still content-scan the history of
# docs/fidelity/**, tools/fidelity/**, baselines/tapes/**,
# tools/tests/test_fidelity_ledger.py, and the path-gap docs
# (docs/BACKLOG_v0.4.0.md, docs/RECOMP_LANDSCAPE_SURVEY_2026-07-10.md) even
# though none of them ever appear in a fresh archive/launch-repo checkout --
# see .gitattributes for why those entries sit outside the marked block. Only
# move a path inside the marked block with the same explicit, written sign-off
# .gitattributes requires for it; an incidental new export-ignore line must
# never silently widen this guard's blind spot again.
pathspecs=(
  .
  ':!scripts/ci/check_public_history_text.sh'
  ':!scripts/ci/check_no_rom_data.sh'
  ':!scripts/ci/check_release_ready.sh'
  ':!scripts/ci/scan_leak_classes.sh'
)
if [ -f .gitattributes ]; then
  while IFS= read -r pat; do
    [ -n "$pat" ] && pathspecs+=( ":(glob,exclude)${pat}" )
  done < <(awk '
    /^[[:space:]]*# --- BEGIN history-scan-exempt/ { active=1; next }
    /^[[:space:]]*# --- END history-scan-exempt/   { active=0; next }
    /^[[:space:]]*#/ { next }
    active && /(^|[[:space:]])export-ignore([[:space:]]|$)/ { print $1 }
  ' .gitattributes)
fi

limit="${GE007_HISTORY_TEXT_HIT_LIMIT:-80}"
if ! history_output="$(git log --all -G"$pattern" \
  --pretty='format:commit %H %s' --name-only -- \
  "${pathspecs[@]}" 2>&1)"; then
  printf '%s\n' "$history_output" >&2
  exit 1
fi

hits="$(printf '%s\n' "$history_output" | awk 'NF' | sed -n "1,${limit}p")"

if [ -n "$hits" ]; then
  printf '%s\n' "$hits"
  echo "FAIL: high-risk text found in reachable git history." >&2
  echo "Rewrite or remove the reachable history before public launch." >&2
  exit 1
fi

echo "PASS: no high-risk private/proprietary text found in reachable git history"
