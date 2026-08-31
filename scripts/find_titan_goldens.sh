#!/usr/bin/env bash
set -euo pipefail

WORKSPACE="${1:-$HOME/Área de Trabalho/TitanWorkspace}"

# Prefer explicitly provided directory.
if [[ -n "${TITAN_GOLDENS:-}" && -d "${TITAN_GOLDENS}" ]]; then
    printf '%s\n' "$TITAN_GOLDENS"
    exit 0
fi

has_triplet() {
    local d="$1"
    [[ -d "$d" ]] || return 1
    local n u l
    n="$(find "$d" -maxdepth 4 -type f -name neutrino.elf -print -quit 2>/dev/null || true)"
    u="$(find "$d" -maxdepth 4 -type f -name titan_ul_engine.elf -print -quit 2>/dev/null || true)"
    l="$(find "$d" -maxdepth 4 -type f -name titan_ul_loader.elf -print -quit 2>/dev/null || true)"
    [[ -n "$n" && -n "$u" && -n "$l" ]] || return 1
    case "$n|$u|$l" in
        *"/Project-Titan/modules/titan-neutrino-engine/"*|*"/Project-Titan/modules/titan-ul-engine/"*) return 1 ;;
    esac
    return 0
}

# Known/common names first, including the singular name used in older workspaces.
for d in \
    "$WORKSPACE/Goldens" \
    "$WORKSPACE/Golden" \
    "$WORKSPACE/goldens" \
    "$WORKSPACE/golden" \
    "$WORKSPACE/GOLDENS" \
    "$WORKSPACE/GOLDEN"; do
    if has_triplet "$d"; then
        printf '%s\n' "$d"
        exit 0
    fi
done

# Then discover any golden-named directory inside TitanWorkspace.
while IFS= read -r -d '' d; do
    if has_triplet "$d"; then
        printf '%s\n' "$d"
        exit 0
    fi
done < <(find "$WORKSPACE" -maxdepth 4 -type d \( -iname 'golden' -o -iname 'goldens' -o -iname 'golden-*' -o -iname '*-golden' \) -print0 2>/dev/null)

# Last safe fallback: a common parent containing the three production binaries,
# but never anything under the engine source trees.
while IFS= read -r -d '' n; do
    case "$n" in
        *"/Project-Titan/modules/titan-neutrino-engine/"*) continue ;;
    esac
    d="$(dirname "$n")"
    for parent in "$d" "$(dirname "$d")" "$(dirname "$(dirname "$d")")"; do
        if has_triplet "$parent"; then
            printf '%s\n' "$parent"
            exit 0
        fi
    done
done < <(find "$WORKSPACE" -maxdepth 6 -type f -name neutrino.elf -print0 2>/dev/null)

exit 1
