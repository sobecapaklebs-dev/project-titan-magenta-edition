#!/usr/bin/env bash
#
# prepare_public_launch_bundle.sh -- collect the non-destructive public-launch
# handoff artifacts in one directory.
#
# This does not rename repositories, push refs, change visibility, or modify
# GitHub issues. It creates a fresh single-root launch repo locally, smoke-tests
# its source archive, exports launch-safe labels/issues, captures GitHub-side
# blocker evidence, and writes a manifest with the next manual commands.
#
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

repo=""
out=""
jobs="${GE007_BUILD_JOBS:-4}"
max_warnings=0
message="Initial public source release"
exclude_numbers=(7 30)
strict_preflight_rom=""
strict_preflight_macos_app=0
strict_preflight_macos_app_bundle_sdl2=0
strict_preflight_macos_app_strict_deployment_target=0
strict_preflight_skip_docker_check=0
author_name="${MGB64_LAUNCH_AUTHOR_NAME:-MGB64 Launch Builder}"
author_email="${MGB64_LAUNCH_AUTHOR_EMAIL:-mgb64-launch@example.invalid}"
committer_name="${MGB64_LAUNCH_COMMITTER_NAME:-$author_name}"
committer_email="${MGB64_LAUNCH_COMMITTER_EMAIL:-$author_email}"
committer_name_explicit=0
committer_email_explicit=0

usage() {
  cat <<'USAGE'
Usage: scripts/prepare_public_launch_bundle.sh [options]

Creates a local launch handoff directory containing:
  - clean-launch-repo/                 fresh single-root repository
  - clean-launch-report.md             clean repo/archive smoke evidence
  - github-launch-evidence.md          current GitHub blocker evidence
  - github-launch-items/               scrubbed labels/open issues export
  - github-launch-items-apply-dry-run.txt
  - PUBLIC_LAUNCH_BUNDLE.md            manifest and next commands
  - logs/                              command transcripts

Options:
  --repo OWNER/REPO       GitHub repository to inspect (default: gh repo view).
  --out DIR               Output bundle directory (default: /tmp/mgb64-public-launch-bundle.*).
  --message MESSAGE       Root commit message for the clean launch repo.
  --author-name NAME      Root commit author name for the clean launch repo.
  --author-email EMAIL    Root commit author email; use a GitHub noreply address
                          for launch attribution.
  --committer-name NAME   Root commit committer name (default: author name).
  --committer-email EMAIL Root commit committer email (default: author email).
  --jobs N                Parallel jobs for archive smoke (default: GE007_BUILD_JOBS or 4).
  --max-warnings N        Archive warning threshold (default: 0).
  --exclude-number N      Open issue number to exclude from export. Defaults to
                          7 and 30; repeat to add more exclusions.
  --no-default-excludes   Do not automatically exclude issues 7 and 30.
  --strict-preflight-rom PATH
                          Also run release_preflight.sh --deep-runtime
                          --strict-ignored inside the clean launch repo using
                          this ROM path. PATH must be outside the generated
                          clean launch repo.
  --strict-preflight-macos-app
                          Include the macOS app asset gate in the optional
                          strict clean-launch preflight.
  --strict-preflight-macos-app-bundle-sdl2
                          Also copy the linked SDL2 dylib into the app bundle
                          during the optional macOS app preflight.
  --strict-preflight-macos-app-strict-deployment-target
                          Fail the optional macOS app preflight if the local
                          SDL2 dylib requires a newer macOS version than the
                          requested app deployment target.
  --strict-preflight-skip-docker-check
                          Skip docker build --check in the optional strict
                          preflight. Do not use for the final launch gate.
  -h, --help              Show this help.

The script is intentionally non-destructive. It prepares evidence and commands;
it does not perform the final repository replacement.
USAGE
}

require_non_negative_int() {
  local name="$1"
  local value="$2"
  case "$value" in
    ''|*[!0-9]*)
      echo "${name} must be a non-negative integer, got: ${value}" >&2
      exit 2
      ;;
  esac
}

require_positive_int() {
  local name="$1"
  local value="$2"
  require_non_negative_int "$name" "$value"
  if [ "$value" -eq 0 ]; then
    echo "${name} must be a positive integer, got: ${value}" >&2
    exit 2
  fi
}

abs_path() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s/%s\n' "$(pwd)" "$1" ;;
  esac
}

run_logged() {
  local log="$1"
  shift

  {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
  } | tee "$log"

  set +e
  "$@" 2>&1 | tee -a "$log"
  local rc=${PIPESTATUS[0]}
  set -e
  if [ "$rc" -ne 0 ]; then
    echo "Command failed with exit status ${rc}; see ${log}" >&2
    exit "$rc"
  fi
}

run_logged_in() {
  local cwd="$1"
  local log="$2"
  shift 2

  {
    printf '+ cd %q &&' "$cwd"
    printf ' %q' "$@"
    printf '\n'
  } | tee "$log"

  set +e
  (cd "$cwd" && "$@") 2>&1 | tee -a "$log"
  local rc=${PIPESTATUS[0]}
  set -e
  if [ "$rc" -ne 0 ]; then
    echo "Command failed with exit status ${rc}; see ${log}" >&2
    exit "$rc"
  fi
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --repo)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      repo="$2"
      shift 2
      ;;
    --out)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      out="$2"
      shift 2
      ;;
    --message)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      message="$2"
      shift 2
      ;;
    --author-name)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      author_name="$2"
      if [ "$committer_name_explicit" -eq 0 ]; then
        committer_name="$author_name"
      fi
      shift 2
      ;;
    --author-email)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      author_email="$2"
      if [ "$committer_email_explicit" -eq 0 ]; then
        committer_email="$author_email"
      fi
      shift 2
      ;;
    --committer-name)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      committer_name="$2"
      committer_name_explicit=1
      shift 2
      ;;
    --committer-email)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      committer_email="$2"
      committer_email_explicit=1
      shift 2
      ;;
    --jobs)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      jobs="$2"
      shift 2
      ;;
    --max-warnings)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      max_warnings="$2"
      shift 2
      ;;
    --exclude-number)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      require_non_negative_int "--exclude-number" "$2"
      exclude_numbers+=("$2")
      shift 2
      ;;
    --no-default-excludes)
      exclude_numbers=()
      shift
      ;;
    --strict-preflight-rom)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      strict_preflight_rom="$(abs_path "$2")"
      shift 2
      ;;
    --strict-preflight-macos-app)
      strict_preflight_macos_app=1
      shift
      ;;
    --strict-preflight-macos-app-bundle-sdl2)
      strict_preflight_macos_app=1
      strict_preflight_macos_app_bundle_sdl2=1
      shift
      ;;
    --strict-preflight-macos-app-strict-deployment-target)
      strict_preflight_macos_app=1
      strict_preflight_macos_app_strict_deployment_target=1
      shift
      ;;
    --strict-preflight-skip-docker-check)
      strict_preflight_skip_docker_check=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      echo "Unknown arg: $1" >&2
      exit 2
      ;;
  esac
done

require_positive_int "--jobs" "$jobs"
require_non_negative_int "--max-warnings" "$max_warnings"
if [ -z "$author_name" ] || [ -z "$author_email" ] || [ -z "$committer_name" ] || [ -z "$committer_email" ]; then
  echo "Launch author/committer names and emails must be non-empty." >&2
  exit 2
fi
# Allowlist, not a denylist of specific personal addresses: the root launch
# commit's author/committer identity must be a GitHub noreply address or the
# placeholder domain, never a real personal inbox of any kind.
case "$author_email" in
  *@users.noreply.github.com|*@example.invalid) ;;
  *)
    echo "Refusing to create a public launch commit with a personal email address (${author_email}); use a GitHub noreply address (e.g. akratch@users.noreply.github.com) or the mgb64-launch@example.invalid placeholder." >&2
    exit 1
    ;;
esac
case "$committer_email" in
  *@users.noreply.github.com|*@example.invalid) ;;
  *)
    echo "Refusing to create a public launch commit with a personal email address (${committer_email}); use a GitHub noreply address (e.g. akratch@users.noreply.github.com) or the mgb64-launch@example.invalid placeholder." >&2
    exit 1
    ;;
esac
if [ -n "$strict_preflight_rom" ] && [ ! -f "$strict_preflight_rom" ]; then
  echo "Strict preflight ROM does not exist: $strict_preflight_rom" >&2
  exit 2
fi

if [ -z "$repo" ]; then
  repo="$(gh repo view --json nameWithOwner --jq '.nameWithOwner' 2>/dev/null || true)"
fi
if [ -z "$repo" ]; then
  echo "Could not resolve GitHub repository; pass --repo OWNER/REPO." >&2
  exit 2
fi

if [ -z "$out" ]; then
  out="$(mktemp -d "${TMPDIR:-/tmp}/mgb64-public-launch-bundle.XXXXXX")"
else
  out="$(abs_path "$out")"
fi

if [ -e "$out" ] && [ -n "$(find "$out" -mindepth 1 -maxdepth 1 2>/dev/null | sed -n '1p')" ]; then
  echo "Output directory already exists and is not empty: $out" >&2
  exit 1
fi
mkdir -p "$out"

logs="$out/logs"
items_dir="$out/github-launch-items"
clean_repo="$out/clean-launch-repo"
clean_report="$out/clean-launch-report.md"
github_evidence="$out/github-launch-evidence.md"
dry_run_log="$out/github-launch-items-apply-dry-run.txt"
manifest="$out/PUBLIC_LAUNCH_BUNDLE.md"
strict_preflight_log=""
mkdir -p "$logs"

source_repo="$(pwd)"
source_head="$(git rev-parse HEAD)"
source_tree="$(git rev-parse HEAD^{tree})"
generated_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

run_logged "$logs/create-public-launch-repo.log" \
  scripts/create_public_launch_repo.sh \
  --out "$clean_repo" \
  --message "$message" \
  --author-name "$author_name" \
  --author-email "$author_email" \
  --committer-name "$committer_name" \
  --committer-email "$committer_email" \
  --smoke-archive \
  --jobs "$jobs" \
  --max-warnings "$max_warnings" \
  --report "$clean_report"

if [ -n "$strict_preflight_rom" ]; then
  strict_preflight_log="$logs/strict-clean-launch-preflight.log"
  strict_preflight_args=(
    env
    "GE007_BUILD_JOBS=$jobs"
    scripts/release_preflight.sh
    --deep-runtime
    --rom "$strict_preflight_rom"
    --strict-ignored
    --jobs "$jobs"
    --max-warnings "$max_warnings"
  )
  if [ "$strict_preflight_macos_app" -eq 1 ]; then
    strict_preflight_args+=(--macos-app)
  fi
  if [ "$strict_preflight_macos_app_bundle_sdl2" -eq 1 ]; then
    strict_preflight_args+=(--macos-app-bundle-sdl2)
  fi
  if [ "$strict_preflight_macos_app_strict_deployment_target" -eq 1 ]; then
    strict_preflight_args+=(--macos-app-strict-deployment-target)
  fi
  if [ "$strict_preflight_skip_docker_check" -eq 1 ]; then
    strict_preflight_args+=(--skip-docker-check)
  fi
  run_logged_in "$clean_repo" "$strict_preflight_log" "${strict_preflight_args[@]}"
fi

export_args=(
  python3
  tools/export_github_launch_items.py
  export
  --repo "$repo"
  --out-dir "$items_dir"
)
for number in "${exclude_numbers[@]}"; do
  export_args+=(--exclude-number "$number")
done
run_logged "$logs/export-github-launch-items.log" "${export_args[@]}"

run_logged "$dry_run_log" \
  python3 tools/export_github_launch_items.py apply \
  --repo "$repo" \
  --input-dir "$items_dir"

run_logged "$logs/prepare-github-launch-evidence.log" \
  env NO_COLOR=1 scripts/prepare_github_launch_evidence.sh \
  --repo "$repo" \
  --out "$github_evidence"

launch_head="$(git -C "$clean_repo" rev-parse HEAD)"
launch_tree="$(git -C "$clean_repo" rev-parse HEAD^{tree})"
launch_count="$(git -C "$clean_repo" rev-list --all --count)"
archive_rel="dist/mgb64-$(git -C "$clean_repo" rev-parse --short=12 HEAD).tar.gz"
archive_path="$clean_repo/$archive_rel"
archive_sha=""
if [ -f "${archive_path}.sha256" ]; then
  archive_sha="$(awk '{ print $1 }' "${archive_path}.sha256")"
elif [ -f "$archive_path" ]; then
  archive_sha="$(shasum -a 256 "$archive_path" | awk '{ print $1 }')"
fi
label_count="$(python3 - "$items_dir/github_launch_items.json" <<'PY'
import json
import sys
payload = json.load(open(sys.argv[1], encoding="utf-8"))
print(len(payload.get("labels", [])))
PY
)"
issue_count="$(python3 - "$items_dir/github_launch_items.json" <<'PY'
import json
import sys
payload = json.load(open(sys.argv[1], encoding="utf-8"))
print(len(payload.get("issues", [])))
PY
)"

{
  printf '# MGB64 Public Launch Bundle\n\n'
  printf '%s\n' "- Generated: \`${generated_at}\`"
  printf '%s\n' "- GitHub repository inspected: \`${repo}\`"
  printf '%s\n' "- Source repository: \`${source_repo}\`"
  printf '%s\n' "- Source HEAD: \`${source_head}\`"
  printf '%s\n' "- Source tree: \`${source_tree}\`"
  printf '%s\n' "- Clean launch repository: \`${clean_repo}\`"
  printf '%s\n' "- Clean launch HEAD: \`${launch_head}\`"
  printf '%s\n' "- Clean launch tree: \`${launch_tree}\`"
  printf '%s\n' "- Clean launch author: \`${author_name} <${author_email}>\`"
  printf '%s\n' "- Clean launch committer: \`${committer_name} <${committer_email}>\`"
  printf '%s\n' "- Clean launch reachable commits: \`${launch_count}\`"
  printf '%s\n' "- Clean source archive: \`${archive_path}\`"
  if [ -n "$archive_sha" ]; then
    printf '%s\n' "- Clean source archive SHA-256: \`${archive_sha}\`"
  fi
  printf '%s\n' "- Exported launch labels: \`${label_count}\`"
  printf '%s\n' "- Exported open launch issues: \`${issue_count}\`"
  printf '\n## Bundle Contents\n\n'
  printf '%s\n' "- \`${clean_repo}\`"
  printf '%s\n' "- \`${clean_report}\`"
  printf '%s\n' "- \`${github_evidence}\`"
  printf '%s\n' "- \`${items_dir}/github_launch_items.json\`"
  printf '%s\n' "- \`${items_dir}/github_launch_items.md\`"
  printf '%s\n' "- \`${dry_run_log}\`"
  printf '%s\n' "- \`${logs}\`"
  if [ -n "$strict_preflight_log" ]; then
    printf '%s\n' "- \`${strict_preflight_log}\`"
  fi
  printf '\n## Non-Destructive Checks Completed\n\n'
  printf '%s\n' "- Clean launch repository has exactly one root commit."
  printf '%s\n' "- Clean launch tree matches the source HEAD tree."
  printf '%s\n' "- Release guard passed inside the clean launch repository."
  printf '%s\n' "- Public history path guard passed inside the clean launch repository."
  printf '%s\n' "- Clean source archive was created, built, warning-gated, and passed ROM-free CTest."
  if [ -n "$strict_preflight_log" ]; then
    printf '%s\n' "- Strict clean-launch preflight passed from the generated repository."
    if [ "$strict_preflight_macos_app" -eq 1 ]; then
      if [ "$strict_preflight_macos_app_strict_deployment_target" -eq 1 ]; then
        printf '%s\n' "- macOS app preflight enforced the requested deployment target."
      else
        printf '%s\n' "- macOS app preflight verified asset hygiene, but did not enforce a distributable deployment target."
      fi
      if [ "$strict_preflight_macos_app_bundle_sdl2" -eq 1 ]; then
        printf '%s\n' "- macOS app preflight bundled the linked SDL2 dylib."
      fi
    fi
  fi
  printf '%s\n' "- GitHub launch labels/open issues were exported through the scrubber."
  printf '%s\n' "- GitHub launch item apply dry-run completed without modifying GitHub."
  printf '%s\n' "- GitHub-side blocker evidence was captured as plain Markdown."
  if [ -z "$strict_preflight_log" ]; then
    printf '\n## Optional Strict Clean-Launch Preflight\n\n'
    printf '%s\n' "This bundle did not run ROM-backed strict preflight. For the final local launch proof, run:"
    printf '\n```sh\n'
    printf '%s\n' "cd \"$clean_repo\""
    printf '%s\n' "scripts/release_preflight.sh --deep-runtime --rom /path/outside/repo/baserom.u.z64 --strict-ignored --macos-app-bundle-sdl2"
    printf '```\n'
    printf '\n%s\n' "For a redistributable macOS app candidate, add \`--macos-app-strict-deployment-target\` with a controlled SDL2 build."
  fi
  printf '\n## Destructive Steps Not Performed\n\n'
  printf '%s\n' "This bundle does not rename repositories, create repositories, push refs, change visibility, import issues, or edit branch protection."
  printf '\n## Replacement Commands To Review\n\n'
  printf '```sh\n'
  printf '%s\n' "# Rename or replace the old private repository only after explicit approval."
  printf '%s\n' "repo=${repo}"
  printf '%s\n' "clean_repo=\"$clean_repo\""
  printf '%s\n' "items_dir=\"$items_dir\""
  printf '%s\n' "expected_source_head=$source_head"
  printf '%s\n' "expected_clean_head=$launch_head"
  printf '%s\n' "expected_clean_tree=$launch_tree"
  printf '%s\n' "backup_name=\"mgb64-prepublic-\$(date -u +%Y%m%d-%H%M%S)\""
  printf '%s\n' 'test "$(git -C "$clean_repo" rev-parse HEAD)" = "$expected_clean_head"'
  printf '%s\n' 'test "$(git -C "$clean_repo" rev-parse HEAD^{tree})" = "$expected_clean_tree"'
  printf '%s\n' 'test "$(git -C "$clean_repo" rev-list --all --count)" = "1"'
  printf '%s\n' 'test -z "$(git -C "$clean_repo" status --porcelain --untracked-files=all)"'
  printf '%s\n' 'test "$(gh api "repos/${repo}/git/ref/heads/main" --jq .object.sha)" = "$expected_source_head"'
  printf '%s\n' 'gh repo rename -R "$repo" "$backup_name" --yes'
  printf '%s\n' "gh repo create \"\$repo\" --private --disable-wiki --description \"A decompilation and native source port of a 1997 Nintendo 64 first-person shooter, for research & preservation. Bring your own ROM - no copyrighted assets included.\""
  printf '%s\n' 'git -C "$clean_repo" remote add launch-clean "git@github.com:${repo}.git"'
  printf '%s\n' 'git -C "$clean_repo" push launch-clean HEAD:refs/heads/main'
  printf '%s\n' 'gh repo edit "$repo" --default-branch main'
  printf '%s\n' 'cd "$clean_repo"'
  printf '%s\n' 'scripts/configure_github_launch_settings.sh --repo "$repo" --yes'
  printf '%s\n' 'python3 tools/export_github_launch_items.py apply --repo "$repo" --input-dir "$items_dir" --yes'
  printf '%s\n' "git ls-remote launch-clean 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'"
  printf '%s\n' 'NO_COLOR=1 scripts/check_github_launch_ready.sh --repo "$repo" --allow-private'
  printf '```\n'
  printf '\n## Final Public Flip Gate\n\n'
  printf '%s\n' "Do not change visibility to public until the strict local launch proof passes on the exact launch commit, GitHub Actions are disabled for the local-CI launch policy, and \`scripts/check_github_launch_ready.sh --repo ${repo} --allow-private\` has no launch blockers other than private visibility plus branch/security endpoints that GitHub only exposes after the public flip."
  printf '%s\n' "Apply branch protection and security settings before the flip when GitHub exposes them; otherwise apply them immediately after the public flip and verify with the final checker below."
  printf '\n```sh\n'
  printf '%s\n' "gh repo edit ${repo} --visibility public --accept-visibility-change-consequences"
  printf '%s\n' "scripts/configure_github_launch_settings.sh --repo ${repo} --yes"
  printf '%s\n' "NO_COLOR=1 scripts/check_github_launch_ready.sh --repo ${repo}"
  printf '```\n'
} > "$manifest"

cat <<EOF

Prepared public launch bundle:
  $out

Manifest:
  $manifest

Clean launch repository:
  $clean_repo

GitHub evidence:
  $github_evidence
EOF
