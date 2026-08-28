#!/usr/bin/env bash
#
# check_release_ready.sh -- public source-release hygiene checks for MGB64.
#
# This wraps the current-tree contamination guard and adds checks that matter
# for public releases: no obvious ROM/media paths anywhere in git history,
# required provenance docs present, and no stale public claims about packaging
# or provenance.
#
set -euo pipefail

if root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
  cd "$root"
  HAVE_GIT=1
else
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "${script_dir}/../.."
  HAVE_GIT=0
fi

fail=0
note() { printf '  \033[31m[VIOLATION]\033[0m %s\n' "$1"; fail=1; }
warn() { printf '  \033[33m[WARN]\033[0m %s\n' "$1"; }

public_file_list() {
  if [ "$HAVE_GIT" -eq 1 ]; then
    git ls-files
    return
  fi

  find . \
    \( -path './.git' -o -path './build' -o -path './build-*' -o -path './dist' -o -path './__pycache__' \) -prune \
    -o -type f -print \
    | sed 's#^\./##' \
    | sort
}

echo "== MGB64 release readiness guard =="

chmod +x scripts/ci/check_no_rom_data.sh
scripts/ci/check_no_rom_data.sh

echo
echo "== Git history filename audit =="
if [ "$HAVE_GIT" -eq 1 ]; then
  # branding/appicon-source.png + branding/appicon-windows.ico: same tracked
  # exception as check_no_rom_data.sh (hand-authored branding, not ROM-derived).
  history_hits=$(git log --all --name-only --pretty=format: \
    | awk 'NF' \
    | sort -u \
    | grep -E '\.(z64|n64|v64|rom|bin|bmp|png|jpe?g|gif|webp|ico|icns|ppm|raw|wav|mp3|ogg|flac|m4a|aac|mp4|mov|m4v|mkv|avi|webm|jsonl|ctl|tbl|aifc|aiff|sbk|seq|cdata|dmg|zip|7z|tar|tgz|gz)$|(^|/)baserom|(^|/)[^/]+\.app(/|$)|ge007_eeprom|ge007\.ini|(^|/)screenshot_[^/]*\.(bmp|png|jpe?g|gif|webp|ppm|raw|jsonl|mp4|mov|m4v|webm)$' \
    | grep -v -E '^branding/appicon-(source\.png|windows\.ico)$' \
    || true)
  if [ -n "$history_hits" ]; then
    while IFS= read -r f; do note "ROM/media/build artifact path found in git history: $f"; done <<< "$history_hits"
  else
    echo "  OK -- no obvious ROM/media/build-artifact filenames found in git history."
  fi
else
  echo "  SKIP -- not a git checkout; archive contents were scanned by the contamination guard."
fi

echo
echo "== Git history text audit =="
if [ "$HAVE_GIT" -eq 1 ]; then
  if scripts/ci/check_public_history_text.sh; then
    :
  else
    note "public git history text guard failed"
  fi
else
  echo "  SKIP -- not a git checkout; archive contents were scanned by the current-tree guards."
fi

echo
echo "== Internal fidelity-program archive isolation =="
if [ "$HAVE_GIT" -eq 1 ]; then
  # D1 (2026-07-10 fidelity review, docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md):
  # the fidelity program is internal-only. .gitattributes export-ignores its
  # trees plus a few named path-gap docs; assert that actually holds for the
  # archive artifact itself (not just the .gitattributes text), so a removed
  # export-ignore line, a typo'd glob, or a new internal file added outside
  # those trees regresses loudly here instead of silently shipping in the next
  # public archive/launch repo. Bare empty-directory entries (e.g.
  # "docs/fidelity/" with nothing after the slash) are excluded: `git archive`
  # emits one of those for every export-ignored directory that ever had
  # tracked content (docs/design/ does the same) -- it reveals a directory
  # name only, never content, and isn't fixable short of patching git itself.
  archive_leak_hits=$(git archive HEAD | tar -t \
    | grep -E '^docs/fidelity/[^/]|^tools/fidelity/[^/]|^baselines/tapes/[^/]|^docs/BACKLOG_v0\.4\.0\.md$|^docs/RECOMP_LANDSCAPE_SURVEY_2026-07-10\.md$|^tools/tests/test_fidelity_ledger\.py$' \
    || true)
  if [ -n "$archive_leak_hits" ]; then
    while IFS= read -r f; do note "internal fidelity-program path is archive-reachable: $f"; done <<< "$archive_leak_hits"
  else
    echo "  OK -- no internal fidelity-program paths (docs/fidelity, tools/fidelity, baselines/tapes, path-gap docs) are archive-reachable."
  fi
else
  echo "  SKIP -- not a git checkout; git archive is not meaningful here."
fi

echo
echo "== Tracked build artifact hygiene =="
artifact_hits=$(public_file_list \
  | grep -E '\.(o|a|so|dylib|dll|exe|pyc|pyo|class|dSYM|app|dmg|zip|7z|tar|tgz|gz)$|(^|/)(__pycache__|build|build-[^/]*|dist)(/|$)|(^|/)(extractor|gzip|armips|n64cksum|report)$' \
  || true)
if [ -n "$artifact_hits" ]; then
  while IFS= read -r f; do note "tracked build/generated artifact path: $f"; done <<< "$artifact_hits"
else
  echo "  OK -- no tracked build/generated artifact paths found."
fi

binary_hits=$(
  while IFS= read -r f; do
    [ -s "$f" ] || continue
    # Exception: hand-authored branding art (not ROM-derived), the single
    # source every platform icon is generated/wrapped from. See the matching
    # exception in .gitignore and scripts/ci/check_no_rom_data.sh.
    case "$f" in
      branding/appicon-source.png|branding/appicon-windows.ico) continue ;;
      # Fidelity determinism baselines: byte-exact controller input tapes
      # (record/replay, FID-0034). Input-events only — no ROM/game data (the
      # contamination guard verifies this) — but intentionally tracked binaries
      # that back the tape_regression / combat-oracle lanes.
      baselines/tapes/*.ge7tape) continue ;;
    esac
    if ! grep -Iq . "$f" 2>/dev/null; then
      printf '%s\n' "$f"
    fi
  done < <(public_file_list)
)
if [ -n "$binary_hits" ]; then
  while IFS= read -r f; do note "tracked binary-looking file: $f"; done <<< "$binary_hits"
else
  echo "  OK -- no tracked binary-looking files found."
fi

echo
echo "== Required public-release docs =="
for f in \
  LICENSE \
  README.md \
  DISCLAIMER.md \
  THIRD_PARTY.md \
  NOTICE.md \
  ROADMAP.md \
  CONTRIBUTING.md \
  CODE_OF_CONDUCT.md \
  SECURITY.md \
  PORT.md \
  RELEASE_NOTES.md \
  docs/BUILDING.md \
  docs/CODING_STYLE.md \
  docs/STATUS.md \
  docs/INSTRUMENTATION.md \
  docs/PROVENANCE_AUDIT.md \
  docs/RELEASE_CHECKLIST.md \
  macos/README.md; do
  if [ ! -s "$f" ]; then
    note "missing or empty release doc: $f"
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "  OK -- required docs are present."
fi

echo
echo "== Repository metadata =="
for f in \
  .gitattributes \
  .dockerignore \
  .gitignore \
  .editorconfig \
  .clang-format \
  .githooks/pre-commit \
  .githooks/pre-push \
  CMakeLists.txt \
  Makefile; do
  if [ ! -s "$f" ]; then
    note "missing or empty repository metadata file: $f"
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "  OK -- required repository metadata is present."
fi

echo
echo "== Docker build-context hygiene =="
for pattern in \
  ".git/" \
  "*.z64" \
  "*.n64" \
  "*.v64" \
  "*.rom" \
  "baserom*" \
  "*.bin" \
  "*.cdata*" \
  "*.eeprom" \
  "*.ctl" \
  "*.tbl" \
  "*.seq" \
  "*.aifc" \
  "*.aiff" \
  "*.seg" \
  "*.log" \
  "*.jsonl" \
  "*.bmp" \
  "*.png" \
  "*.jpg" \
  "*.jpeg" \
  "*.gif" \
  "*.webp" \
  "*.ppm" \
  "*.raw" \
  "*.wav" \
  "*.mp3" \
  "*.ogg" \
  "*.flac" \
  "*.m4a" \
  "*.aac" \
  "*.mp4" \
  "*.mov" \
  "*.m4v" \
  "*.mkv" \
  "*.avi" \
  "*.webm" \
  "assets/images/split/" \
  "assets/obseg/bg/*_all_p.c" \
  "tools/ido5.3_recomp/*" \
  "build/" \
  "dist/"; do
  if ! grep -Fxq -- "$pattern" .dockerignore; then
    note "missing Docker build-context ignore pattern: $pattern"
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "  OK -- Docker build context excludes ROMs, extracted assets, and build output."
fi

echo
echo "== Third-party provenance files =="
for f in \
  lib/glad/LICENSE \
  lib/glad/README.md \
  tools/asm-processor/LICENSE \
  tools/extractor/README.md \
  tools/gzipsrc/COPYING \
  tools/gzipsrc/README.md \
  tools/ido5.3_recomp/README.md \
  tools/mktex/LICENSE.perfect_dark \
  tools/mktex/PROVENANCE.md; do
  if [ ! -s "$f" ]; then
    note "missing or empty third-party provenance file: $f"
  fi
done
if python3 tools/check_third_party_notices.py --repo-root .; then
  echo "  OK -- required third-party provenance files are present."
else
  note "third-party notice guard failed"
fi

echo
echo "== N64 IPL3 boot placeholder =="
if [ ! -s src/bootcode.s ]; then
  note "missing public IPL3 boot placeholder: src/bootcode.s"
else
  if grep -Fq 'Super Mario 64 (J) disassembly' src/bootcode.s; then
    note "src/bootcode.s contains old SM64/n64split provenance text"
  fi
  if grep -Eq '^[[:space:]]*\.byte[[:space:]]+0x' src/bootcode.s; then
    note "src/bootcode.s contains tracked boot/font byte payloads; keep it as a placeholder"
  fi
  if ! grep -Fq 'IPL3 boot-font data removed' src/bootcode.s; then
    note "src/bootcode.s does not state that IPL3 boot-font data was removed"
  fi
  if ! grep -Fq 'src/bootcode.s' NOTICE.md || ! grep -Fq 'src/bootcode.s' THIRD_PARTY.md; then
    note "src/bootcode.s placeholder is not disclosed in NOTICE.md and THIRD_PARTY.md"
  fi
fi
if [ "$fail" -eq 0 ]; then
  echo "  OK -- public bootcode source path is an asset-free placeholder."
fi

echo
echo "== GitHub contributor scaffolding =="
for f in \
  .github/CODEOWNERS \
  .github/dependabot.yml \
  .github/PULL_REQUEST_TEMPLATE.md \
  .github/ISSUE_TEMPLATE/config.yml \
  .github/ISSUE_TEMPLATE/bug_report.md \
  .github/ISSUE_TEMPLATE/build_failure.md \
  .github/ISSUE_TEMPLATE/parity_report.md \
  .github/ISSUE_TEMPLATE/platform_validation.md \
  .github/ISSUE_TEMPLATE/provenance_cleanup.md \
  .github/ISSUE_TEMPLATE/validation_task.md \
  .github/workflows/ci.yml \
  .github/workflows/macos-release.yml; do
  if [ ! -s "$f" ]; then
    note "missing or empty GitHub contributor scaffold: $f"
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "  OK -- required GitHub contributor scaffolding is present."
fi

echo
echo "== GitHub Actions local-CI policy =="
workflow_trigger_hits="$(
  python3 - <<'PY'
from pathlib import Path
import re

bad = {"push", "pull_request", "pull_request_target", "schedule"}
workflow_dir = Path(".github/workflows")
if not workflow_dir.is_dir():
    raise SystemExit

def strip_comment(line: str) -> str:
    # Good enough for this repo's workflow files: comments are not embedded in
    # quoted scalars in the `on:` block.
    return line.split("#", 1)[0].rstrip()

for workflow in sorted(workflow_dir.glob("*.y*ml")):
    lines = workflow.read_text(encoding="utf-8").splitlines()
    in_on = False
    on_indent = 0
    for number, line in enumerate(lines, 1):
        clean = strip_comment(line)
        if not clean.strip():
            continue
        indent = len(clean) - len(clean.lstrip(" "))
        text = clean.strip()

        if in_on and indent <= on_indent:
            in_on = False

        if not in_on:
            if text.startswith("on:"):
                rest = text[3:].strip()
                if rest:
                    tokens = re.findall(r"[A-Za-z_]+", rest)
                    if any(token in bad for token in tokens):
                        print(f"{workflow}:{number}:{text}")
                else:
                    in_on = True
                    on_indent = indent
            continue

        key = text.split(":", 1)[0].strip().strip("'\"")
        list_item = text[1:].strip().strip("'\"") if text.startswith("-") else ""
        if key in bad or list_item in bad:
            print(f"{workflow}:{number}:{text}")
PY
)"
if [ -n "$workflow_trigger_hits" ]; then
  while IFS= read -r hit; do
    note "GitHub Actions workflow has an automatic hosted trigger; public repo policy is local-CI only: $hit"
  done <<< "$workflow_trigger_hits"
else
  echo "  OK -- GitHub Actions workflows are manual-only local-CI mirrors."
fi

workflow_action_hits="$(
  if [ -d .github/workflows ]; then
    while IFS= read -r workflow; do
      line_no=0
      while IFS= read -r line || [ -n "$line" ]; do
        line_no=$((line_no + 1))
        no_comment="${line%%#*}"
        ref="$(printf '%s\n' "$no_comment" | sed -nE 's/^[[:space:]]*(-[[:space:]]*)?uses:[[:space:]]*([^[:space:]]+).*/\2/p')"
        [ -n "$ref" ] || continue
        case "$ref" in
          ./*|../*) continue ;;
        esac
        if [[ "$ref" != *@* ]]; then
          printf '%s:%s:%s\n' "$workflow" "$line_no" "$ref"
          continue
        fi
        action_ref="${ref##*@}"
        if [[ ! "$action_ref" =~ ^[0-9A-Fa-f]{40}$ ]]; then
          printf '%s:%s:%s\n' "$workflow" "$line_no" "$ref"
        fi
      done < "$workflow"
    done < <(find .github/workflows -type f \( -name '*.yml' -o -name '*.yaml' \) | sort)
  fi
)"
if [ -n "$workflow_action_hits" ]; then
  while IFS= read -r hit; do
    note "GitHub Actions workflow reference is not pinned to a full commit SHA: $hit"
  done <<< "$workflow_action_hits"
else
  echo "  OK -- GitHub Actions workflow references are pinned to full commit SHAs."
fi

echo
echo "== Public documentation links =="
if python3 tools/check_markdown_links.py --repo-root .; then
  :
else
  note "Markdown link guard failed"
fi

echo
echo "== Public shell tool syntax =="
if python3 tools/check_shell_syntax.py --repo-root .; then
  :
else
  note "shell syntax guard failed"
fi

echo
echo "== Release helper scripts =="
for f in \
  scripts/release_preflight.sh \
  scripts/install_git_hooks.sh \
  scripts/ci/check_high_risk_ignored_artifacts.sh \
  scripts/ci/check_public_history_text.sh \
  scripts/make_public_source_archive.sh \
  scripts/smoke_public_source_archive.sh \
  scripts/check_github_launch_ready.sh \
  macos/Scripts/build_app_bundle.sh \
  macos/Scripts/build_universal.sh \
  macos/Scripts/verify_asset_free.sh \
  macos/Scripts/generate_app_icon.py; do
  if [ ! -x "$f" ]; then
    note "missing or non-executable release helper script: $f"
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "  OK -- release helper scripts are present and executable."
fi

echo
echo "== macOS app test harness =="
for f in \
  macos/Tests/run_tests.sh \
  macos/Tests/test_asset_free_verifier.sh \
  macos/Tests/test_rom_validation.c \
  macos/Tests/test_stubs.c; do
  if [ ! -s "$f" ]; then
    note "missing or empty macOS app test harness file: $f"
  fi
done
for f in \
  macos/Tests/run_tests.sh \
  macos/Tests/test_asset_free_verifier.sh; do
  if [ ! -x "$f" ]; then
    note "missing executable bit on macOS app test harness script: $f"
  fi
done
if [ "$fail" -eq 0 ]; then
  echo "  OK -- macOS app test harness is present."
fi

echo
echo "== Native SDK surface =="
if python3 tools/check_native_sdk_surface.py --repo-root .; then
  :
else
  note "native SDK surface guard failed"
fi

echo
echo "== SDK compatibility inventory =="
if python3 tools/check_sdk_inventory.py --repo-root .; then
  :
else
  note "SDK compatibility inventory guard failed"
fi

echo
echo "== Native gameplay/helper stub surface =="
if python3 tools/check_native_stub_surface.py --repo-root .; then
  :
else
  note "native gameplay/helper stub surface guard failed"
fi

echo
echo "== Public claim alignment =="
overbroad_provenance_claim_hits=$(grep -RInE \
  'from[- ]scratch.{0,80}(decompilation|port)|fully[ -]clean[- ]room|clean[- ]room.{0,80}(decompilation|port|project|repository)' \
  README.md RELEASE_NOTES.md PORT.md NOTICE.md DISCLAIMER.md macos/README.md 2>/dev/null || true)
if [ -n "$overbroad_provenance_claim_hits" ]; then
  while IFS= read -r hit; do note "overbroad provenance/clean-room public claim: $hit"; done <<< "$overbroad_provenance_claim_hits"
else
  echo "  OK -- no overbroad from-scratch/clean-room public claims found."
fi

macos_claim_hits=$(grep -RInE \
  'first-launch ROM picker release|Prebuilt distributables are[[:space:]]+\*\*unsigned\*\*|signed/notarized distributables are available' \
  README.md RELEASE_NOTES.md PORT.md docs/*.md macos/README.md 2>/dev/null || true)
if [ -n "$macos_claim_hits" ]; then
  while IFS= read -r hit; do note "stale signed/prebuilt macOS public claim: $hit"; done <<< "$macos_claim_hits"
else
  echo "  OK -- no stale signed/prebuilt macOS release claims found in public docs."
fi

macos_workflow_claim_hits=$(grep -RInE \
  'handle the full release pipeline|Requires the following repository secrets|skip_notarize|name:[[:space:]]+macOS Build$|Build macOS Artifacts|Upload library artifact|Triggered on version tags \(e\.g\., v1\.0\.0\)' \
  .github/workflows/macos-release.yml macos/README.md 2>/dev/null || true)
if [ -n "$macos_workflow_claim_hits" ]; then
  while IFS= read -r hit; do note "stale macOS workflow/distribution claim: $hit"; done <<< "$macos_workflow_claim_hits"
else
  echo "  OK -- no stale macOS workflow/distribution claims found."
fi

packaging_placeholder_hits=$(grep -RInE \
  'PLACEHOLDER_SHA256|github\.com/mgb64/mgb64|mgb64\.dev|release/download/.+\.dmg|brew install --cask mgb64|Metal rendering' \
  Casks .github README.md RELEASE_NOTES.md PORT.md docs/*.md macos/README.md macos/Resources/*.plist 2>/dev/null || true)
if [ -n "$packaging_placeholder_hits" ]; then
  while IFS= read -r hit; do note "stale or placeholder packaging metadata: $hit"; done <<< "$packaging_placeholder_hits"
else
  echo "  OK -- no placeholder packaged-release metadata found."
fi

if [ "$HAVE_GIT" -eq 1 ]; then
  sdk_notice_count=$(git grep -liE 'UNPUBLISHED[[:space:]]+PROPRIETARY|may not be disclosed|without the prior written (permission|consent)|RESTRICTED[[:space:]]+RIGHTS|subparagraph \(c\)\(1\)\(ii\) of the Rights' -- . ':!scripts/ci/check_no_rom_data.sh' ':!scripts/ci/check_release_ready.sh' 2>/dev/null || true)
else
  sdk_notice_count=$(
    while IFS= read -r f; do
      case "$f" in
        scripts/ci/check_no_rom_data.sh|scripts/ci/check_release_ready.sh) continue ;;
      esac
      if grep -Iq . "$f" 2>/dev/null && grep -IqE 'UNPUBLISHED[[:space:]]+PROPRIETARY|may not be disclosed|without the prior written (permission|consent)|RESTRICTED[[:space:]]+RIGHTS|subparagraph \(c\)\(1\)\(ii\) of the Rights' "$f" 2>/dev/null; then
        printf '%s\n' "$f"
      fi
    done < <(public_file_list)
  )
fi
sdk_notice_count=$(printf '%s\n' "$sdk_notice_count" | awk 'NF {count++} END {print count + 0}')
if [ "${sdk_notice_count:-0}" -gt 0 ]; then
  note "proprietary SDK notice text remains in tracked source: $sdk_notice_count path(s)"
else
  echo "  OK -- no proprietary SDK notice text found in tracked source."
fi

if [ "$fail" -ne 0 ]; then
  echo
  echo "Release readiness guard FAILED."
  exit 1
fi

echo
echo "Release readiness guard passed."
