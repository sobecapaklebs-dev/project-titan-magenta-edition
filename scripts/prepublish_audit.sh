#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-.}"
cd "$ROOT"

blockers=0
warnings=0

block() { echo "BLOCKER: $*"; blockers=$((blockers + 1)); }
warn()  { echo "WARN: $*"; warnings=$((warnings + 1)); }
allow() { echo "ALLOW: $*"; }

echo "=== Project Titan public-source prepublish audit ==="

# Commercial/proprietary game content must never be present.
while IFS= read -r -d '' f; do
    case "$f" in
        ./apps/project-nes/data/roms/ProjectNesTest.nes)
            expected="1c93186e908aff0f89ce763e728b4b901faf519c6d0656810c61f50e1beae9f8"
            actual="$(sha256sum "$f" | awk '{print $1}')"
            if [[ "$actual" == "$expected" ]]; then
                allow "first-party deterministic Project NES diagnostic ROM: $f"
            else
                block "ProjectNesTest.nes identity changed: $actual"
            fi
            ;;
        *)
            block "commercial/private game-media candidate: $f"
            ;;
    esac
done < <(
    find . -type f \
      \( -iname '*.z64' -o -iname '*.v64' -o -iname '*.n64' \
         -o -iname '*.iso' -o -iname '*.cso' -o -iname '*.chd' \) \
      -print0 2>/dev/null
)

# Three tiny TMOD files are deterministic parser/ISA unit-test fixtures and
# have been identity-stable across prior source manifests. They are source-test
# data, not distributable application packages.
declare -A TEST_TMOD_SHA=(
    ["./kernel/tests/fixtures/hello_titan_mips32.tmod"]="baecc61dec6b6ce86b8da7365985e59fe09347d5dc0a191e978282222aabb4ac"
    ["./kernel/tests/fixtures/hello_titan_r5900_bad_cop1.tmod"]="839d82deefa136913056c2eb738c14f4e63d5776e31d4dd4d162030675dec218"
    ["./kernel/tests/fixtures/hello_titan_r5900_contract.tmod"]="efba9f892cded02aefeb6ece89264ab53baf7583c8df54ec8bd95dd53817d057"
)

while IFS= read -r -d '' f; do
    case "$f" in
        ./kernel/tests/fixtures/*.tmod)
            expected="${TEST_TMOD_SHA[$f]:-}"
            actual="$(sha256sum "$f" | awk '{print $1}')"
            if [[ -n "$expected" && "$actual" == "$expected" ]]; then
                allow "identity-locked Titan test fixture: $f"
            else
                block "unknown or changed TMOD fixture: $f sha256=$actual"
            fi
            ;;
        ./third_party/*)
            # Vendored target payloads are handled by the canonical Titan
            # source auditor, which identity-locks the approved FMCB resources.
            ;;
        *)
            block "generated target/release payload in source export: $f"
            ;;
    esac
done < <(
    find . -type f \
      \( -iname '*.elf' -o -iname '*.tmod' -o -iname '*.tpkg' \
         -o -iname '*.pkg' -o -iname '*.pup' -o -iname '*.fw' \) \
      -print0 2>/dev/null
)

# .titan-tls is a generated local BearSSL build/work tree, not canonical source.
[[ ! -d ./kernel/third_party/.titan-tls ]] ||
    block "generated kernel/third_party/.titan-tls tree is present"

# Real credential patterns.
if grep -RIlE --exclude-dir=.git --exclude='prepublish_audit.sh' \
    '(ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{50,}|AKIA[0-9A-Z]{16}|-----BEGIN (RSA |OPENSSH |EC )?PRIVATE KEY-----)' . \
    > /tmp/titan-public-token-hits.$$ 2>/dev/null; then
    while IFS= read -r f; do block "possible credential/private key in $f"; done < /tmp/titan-public-token-hits.$$
fi
rm -f /tmp/titan-public-token-hits.$$

# GitHub hard limit is 100 MiB; leave headroom.
while IFS= read -r -d '' f; do
    block "file exceeds 95 MiB: $f"
done < <(find . -type f -not -path './.git/*' -size +95M -print0)

# Private menu-music asset is deliberately not part of the public release.
[[ ! -f ./runtime/TITAN/audio/magenta_melody.wav ]] ||
    block "private/non-redistributed Magenta Melody is present"

# Build/distribution trees are source-export mistakes.
[[ ! -d ./dist ]] || block "dist/ directory is present"
if find . -type d -name '.install-*' -print -quit | grep -q .; then
    block "installer .install-* staging directory present"
fi


# Development backup artifacts must never ship in the public source snapshot.
while IFS= read -r -d '' d; do
    block "development backup directory in public export: $d"
done < <(
    find . -maxdepth 1 -type d \
      \( -name '.release_app_artwork_backup_*' \
         -o -name '.release_app_artwork_p162_rebase_backup_*' \) \
      -print0 2>/dev/null
)

while IFS= read -r -d '' f; do
    case "$f" in
        ./third_party/*) ;;
        *) block "development backup file in public export: $f" ;;
    esac
done < <(
    find . -type f \
      \( -name '*.bak' -o -name '*.orig' -o -name '*.rej' \) \
      -print0 2>/dev/null
)

# Absolute local paths are warnings because upstream test scripts/docs can
# legitimately contain examples or leak-detection fixtures. Review them, but
# don't conflate them with credentials.
if grep -RIlE --exclude-dir=.git --exclude='prepublish_audit.sh' \
    '/home/[^/]+/|/run/media/[^/]+/' . \
    > /tmp/titan-public-localpaths.$$ 2>/dev/null; then
    while IFS= read -r f; do warn "absolute developer-machine path in $f"; done < /tmp/titan-public-localpaths.$$
fi
rm -f /tmp/titan-public-localpaths.$$

if [[ ! -f LICENSE && ! -f LICENSE.md && ! -f COPYING ]]; then
    warn "top-level Project Titan license has not yet been selected"
fi

echo "BLOCKERS=$blockers WARNINGS=$warnings"
if (( blockers > 0 )); then
    echo "PREPUBLISH_AUDIT=FAIL"
    exit 1
fi
echo "PREPUBLISH_AUDIT=PASS"
