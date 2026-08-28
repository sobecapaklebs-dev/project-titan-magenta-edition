#!/usr/bin/env bash
#
# scan_leak_classes.sh -- scan a BOUNDED set of commits for the never-leak text
# classes: private absolute paths, personal email, credentials, and proprietary
# SDK-notice text.
#
# This is the shared engine for two callers:
#   * .githooks/pre-push        -- structural gate on EVERY push to the public
#                                  remote (the day-to-day `git push -u origin
#                                  fix/whatever` topic-branch lane included), so
#                                  no leak-class text can reach the public repo
#                                  without a script being run by hand.
#   * scripts/publish_public.sh -- belt-and-suspenders scan of the exact pushed
#                                  range, alongside the whole-history guard.
#
# Unlike scripts/ci/check_public_history_text.sh (which scans ALL reachable
# history, diff-content only), this scans a caller-supplied list of NEW commits
# -- both their diff CONTENT and their commit MESSAGES -- which is precisely the
# surface a push introduces. It walks nothing: only the commits you name.
#
# Doctrine (owner ruling 2026-07-11, docs/fidelity/ESCALATIONS.md, resolved
# C1/D1): the fidelity trees (docs/fidelity/**, tools/fidelity/**,
# baselines/tapes/**) and the AI-authorship / session commit-message trailers are
# ACCEPTED public. This scan MUST NOT block on them -- it blocks ONLY the
# never-leak classes below. That is why the pattern here deliberately does NOT
# include "Claude" / session / agent-memory tokens (every commit carries a
# Co-Authored-By trailer; matching those would refuse every push).
#
# Usage:
#   scripts/ci/scan_leak_classes.sh <commit-sha> [<commit-sha> ...]
#
# Exit: 0 clean (or nothing to scan), 1 on a hit (offending commits printed to
# stderr), 2 on a usage/repo error.
#
set -euo pipefail

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "scan_leak_classes: not inside a git worktree" >&2
  exit 2
fi
cd "$(git rev-parse --show-toplevel)"

if [ "$#" -eq 0 ]; then
  echo "scan_leak_classes: no commits supplied -- nothing to scan"
  exit 0
fi
commits=("$@")

# --- never-leak text classes -------------------------------------------------
# Kept in lock-step with scripts/ci/check_public_history_text.sh, but DELIBERATELY
# narrowed to the doctrine's "never leak going forward" set: private absolute
# paths, personal email, credentials, and proprietary SDK-notice text. It does
# NOT carry the prose markers ("session handoff", "agent memory", ...) from the
# whole-history guard: those are discussion-topics the ACCEPTED-public fidelity
# trees may legitimately mention, and matching them would refuse accepted pushes.
# The ''-splits keep the guard's own pattern strings from matching themselves.
# /home/<user>/ requires a trailing segment so it means a real home directory,
# not an incidental ".../home/..." mid-path (e.g. a scratch $WORK/home/.gitconfig
# or a CI /home path); /Users/<user> is the macOS dev-machine leak vector.
private_path='(/Users/[^[:space:]]+|/home/[A-Za-z0-9._-]+/[^[:space:]]|Desktop/dev|github\.com/akratch/007)'
secret='(AKIA[0-9A-Z]{16}|ghp_[0-9A-Za-z_]{36,}|github_pat_[0-9A-Za-z_]+|xox[baprs]-[0-9A-Za-z-]+|sk-[A-Za-z0-9]{20,}|BEGIN (RSA|OPENSSH|EC|DSA) PRIVATE KEY)'
sdk_notice='(UNPUBLISHED[[:space:]]+PROPRI''ETARY|may not be disclo''sed|without the prior written (permission|cons''ent)|RESTRICTED[[:space:]]+RIG''HTS|subparagraph \(c\)\(1\)\(ii\) of the Rig''hts)'
# Personal email: any personal/free-provider mailbox. The owner's own git
# identity is an @users.noreply.github.com address (NOT a personal provider), so
# it never matches; the AI-authorship trailers use @anthropic.com (likewise not
# matched). Third-party OSS attribution addresses (e.g. an author's @gmail in a
# LICENSE/NOTICE file) are excluded by PATH from the content scan below, so they
# do not false-positive.
personal_email='[A-Za-z0-9._%+-]+@(gmail|googlemail|outlook|hotmail|live|msn|yahoo|ymail|icloud|me|mac|proton|protonmail|pm|aol|gmx|zoho|fastmail|hey|mail|tutanota)\.(com|me|net|org)'

# Path + secret + proprietary notice: scanned in every file except the guard/hook
# scripts that embed these very patterns (they would match themselves).
path_secret_notice="(${private_path}|${secret}|${sdk_notice})"
guard_excludes=(
  ':(exclude)scripts/ci/scan_leak_classes.sh'
  ':(exclude)scripts/ci/check_public_history_text.sh'
  ':(exclude)scripts/ci/check_no_rom_data.sh'
  ':(exclude)scripts/ci/check_release_ready.sh'
  ':(exclude).githooks/pre-push'
  ':(exclude).githooks/pre-commit'
)
# License/notice files legitimately carry third-party author emails; exclude them
# from the EMAIL sub-scan only (paths/secrets are still scanned there via the
# pass above).
license_excludes=(
  ':(glob,exclude)**/LICENSE'
  ':(glob,exclude)**/LICENSE.*'
  ':(glob,exclude)**/COPYING'
  ':(glob,exclude)**/COPYING.*'
  ':(exclude)NOTICE.md'
  ':(glob,exclude)**/*.license'
)

fail=0
report() {  # header  git-output
  [ -n "$2" ] || return 0
  printf 'LEAK (%s):\n' "$1" >&2
  printf '%s\n' "$2" | sed 's/^/  /' >&2
  fail=1
}

# Content scans are ADDED-lines-only (2026-07-17): `git log -G` matches added
# AND removed lines, which blocked the one commit class doctrine WANTS pushed —
# a scrub that deletes already-public leak text (its removal diff necessarily
# quotes the text it removes; e.g. the personal-email scrub 28839aa). "Never
# leak going forward" means what a push ADDS to the public remote; removals
# introduce nothing new there. Commit MESSAGES are still scanned in full below.
# Emits the same shape as the old -G scan: "<h> <subject>" then hit files.
scan_added_lines() {  # $1=pattern, then pathspec excludes...
  local pattern="$1"; shift
  local out="" c files
  for c in "${commits[@]}"; do
    files="$(git show --format= --unified=0 "$c" -- . "$@" 2>/dev/null |
      PAT="$pattern" awk '
        /^\+\+\+ b\// { f = substr($0, 7); next }
        /^\+/ && !/^\+\+\+/ {
          if (substr($0, 2) ~ ENVIRON["PAT"] && !(f in seen)) { seen[f] = 1; print f }
        }')"
    if [ -n "$files" ]; then
      out+="$(git log --no-walk -1 --pretty='format:%h %s' "$c")"$'\n'"$files"$'\n'
    fi
  done
  printf '%s' "$out"
}

# (1) diff content (added lines): private paths + credentials + proprietary notice.
c_ps="$(scan_added_lines "$path_secret_notice" "${guard_excludes[@]}")"
report "private path / credential / proprietary text added by a pushed diff" "$c_ps"

# (2) diff content (added lines): personal email (excluding license/notice files).
c_em="$(scan_added_lines "$personal_email" "${guard_excludes[@]}" "${license_excludes[@]}")"
report "personal email address added by a pushed diff" "$c_em"

# (3) commit messages: all classes (trailers never match -- see header).
all_pattern="(${private_path}|${secret}|${sdk_notice}|${personal_email})"
c_msg="$(git log --no-walk "${commits[@]}" -E --grep="$all_pattern" \
         --pretty='format:%h %s' 2>/dev/null || true)"
report "never-leak text in a pushed commit message" "$c_msg"

if [ "$fail" -ne 0 ]; then
  {
    echo
    echo "scan_leak_classes: REFUSED -- never-leak text found in the commits being pushed."
    echo "  Blocked classes: private absolute paths, personal email, credentials,"
    echo "  proprietary SDK-notice text. (Fidelity trees + AI trailers are accepted; not blocked.)"
    echo "  Rewrite the offending commit(s) to remove the text before pushing to the public remote."
  } >&2
  exit 1
fi

echo "scan_leak_classes: clean (${#commits[@]} commit(s) scanned)"
exit 0
