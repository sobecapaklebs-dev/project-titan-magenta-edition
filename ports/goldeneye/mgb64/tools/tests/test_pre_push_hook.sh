#!/usr/bin/env bash
#
# test_pre_push_hook.sh -- structural proof that .githooks/pre-push refuses a
# push carrying never-leak text to the public remote, and allows a clean one.
#
# Entirely ROM-free and offline: a throwaway scratch repo pushes to a LOCAL BARE
# repo standing in for the public remote (named `origin`, which the hook treats
# as public), with core.hooksPath pointed at this repo's REAL tracked hooks. It
# never touches origin or any network remote.
#
# This reproduces the C3 leak-class scenario end-to-end (topic-branch push +
# would-be server-side merge) and proves the hook closes it at the git layer.
#
# Wired as the ROM-free ctest `pre_push_leak_guard` (see CMakeLists.txt).
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOOKS="$REPO_ROOT/.githooks"
[[ -x "$HOOKS/pre-push" ]] || { echo "FATAL: not executable: $HOOKS/pre-push" >&2; exit 2; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/pre_push_hook_test_XXXXXX")"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

pass=0; fail=0
ok()  { printf '  \033[32mPASS\033[0m %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  \033[31mFAIL\033[0m %s\n' "$1"; fail=$((fail+1)); }

export HOME="$WORK/home"; mkdir -p "$HOME"
export GIT_CONFIG_GLOBAL="$WORK/home/.gitconfig"; export GIT_CONFIG_SYSTEM=/dev/null
git config --global user.email "test@example.invalid"
git config --global user.name "Hook Test"
git config --global init.defaultBranch main
git config --global commit.gpgsign false
git config --global advice.detachedHead false

# The leak fixtures below are reconstructed AT RUNTIME from parts, with PLACEHOLDER
# (non-owner) values, so this tracked test file itself carries none of the literal
# never-leak strings it plants -- it stays fully in-scope for the text guards, and
# nothing here is a real private path / email / secret. `_slash`/`_at`/`_us` hold
# the byte that would otherwise complete a guard-matching literal in the source.
_slash='/'; _at='@'; _us='_'
_ai_trailer="Co-Authored-By: ${_C:-C}laude Opus 4.8 <noreply${_at}anthropic.com>"   # AI trailer (accepted, not blocked)
_leak_path="${_slash}Users/example/project/secret.log"          # -> a placeholder absolute home path
_leak_email="leaker.person${_at}gmail.com"                       # -> a (placeholder) personal gmail
_leak_cred="ghp${_us}0123456789abcdefghijABCDEFGHIJ0123456789"  # -> a (fake) GitHub PAT shape

REPO="$WORK/repo"; BARE="$WORK/public.git"
git init -q "$REPO"
git init -q --bare "$BARE"
( cd "$REPO"
  git remote add origin "$BARE"           # the hook treats remote `origin` as public
  git config core.hooksPath "$HOOKS"      # wire the REAL tracked hooks
  echo "hello" > README.md; git add README.md
  git commit -q --no-verify -m "initial clean commit

${_ai_trailer}"
  git push -q --no-verify origin main )   # seed baseline (setup only)

# push_topic BRANCH  -> echoes rc; commits are created with --no-verify (we are
# exercising pre-PUSH, not pre-commit).
push_topic() {  # branch  commit-fn
  ( cd "$REPO"
    git switch -q main
    git switch -qc "$1"
    "$2"
    git push -u origin "$1" >/dev/null 2>&1 && echo 0 || echo $?
  )
}

commit_clean()  { echo "a clean line" >> README.md; git add README.md; git commit -q --no-verify -m "docs: clean change

${_ai_trailer}"; }
commit_path()   { printf 'dbg("%s");\n' "$_leak_path" > d.c; git add d.c; git commit -q --no-verify -m "feat: debug"; }
commit_email()  { git commit -q --no-verify --allow-empty -m "chore: ping ${_leak_email} about it"; }
commit_cred()   { printf '%s\n' "$_leak_cred" > tok.txt; git add tok.txt; git commit -q --no-verify -m "add token"; }

echo "== pre-push hook: leak-class refusal tests =="

rc="$(push_topic fix/clean commit_clean)"
[[ "$rc" == 0 ]] && ok "clean topic branch ALLOWED" || bad "clean topic branch should be allowed (rc=$rc)"

rc="$(push_topic fix/leak-path commit_path)"
[[ "$rc" != 0 ]] && ok "planted /Users path REFUSED" || bad "planted /Users path should be refused"

rc="$(push_topic fix/leak-email commit_email)"
[[ "$rc" != 0 ]] && ok "personal email in message REFUSED" || bad "personal email should be refused"

rc="$(push_topic fix/leak-cred commit_cred)"
[[ "$rc" != 0 ]] && ok "planted credential REFUSED" || bad "credential should be refused"

# Only the clean branch may have reached the bare "public" remote.
landed="$(git --git-dir="$BARE" for-each-ref --format='%(refname)' 2>/dev/null | sort | tr '\n' ' ')"
if [[ "$landed" == *"refs/heads/fix/clean"* \
   && "$landed" != *"leak-path"* && "$landed" != *"leak-email"* && "$landed" != *"leak-cred"* ]]; then
  ok "only the clean branch landed on the fake public remote"
else bad "unexpected refs on fake remote: $landed"; fi

# Direct push to public main WITHOUT the gate marker is refused (routing nudge);
# WITH the marker (as publish_public.sh sets) it is allowed.
rc="$( cd "$REPO"; git switch -q main; echo x >> README.md; git add README.md
       git commit -q --no-verify -m "chore: bump"
       git push origin main >/dev/null 2>&1 && echo 0 || echo $? )"
[[ "$rc" != 0 ]] && ok "ungated direct push to public main REFUSED" || bad "ungated main push should be refused"
rc="$( cd "$REPO"; MGB64_PUBLISH_GATE=1 git push origin main >/dev/null 2>&1 && echo 0 || echo $? )"
[[ "$rc" == 0 ]] && ok "gated push to public main ALLOWED" || bad "gated main push should be allowed (rc=$rc)"

echo
echo "== pre-push hook: ${pass} passed, ${fail} failed =="
[[ "$fail" -eq 0 ]] || exit 1
