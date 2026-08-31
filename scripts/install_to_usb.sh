#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: install_to_usb.sh --target /run/media/USER/PS [--package DIR]

Installs a previously built Project Titan Magenta Edition runtime. The package
must contain kernel.elf, titan.elf, both validated UL compatibility paths
(TITAN/titan_ul_engine.elf and TITAN/titan_ul_loader.elf), and neutrino/neutrino.elf.
Files are staged, hashed, published by sibling renames,
and covered by a recovery journal. The canonical kernel path is always the
root of the device; TITAN/kernel.elf is never installed.
USAGE
}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TARGET=""
PACKAGE="$ROOT/dist/Project Titan Magenta Edition"

while (($#)); do
    case "$1" in
        --target) TARGET=${2:?missing target}; shift 2 ;;
        --package) PACKAGE=${2:?missing package}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -n "$TARGET" ]] || { usage >&2; exit 2; }
[[ -d "$TARGET" ]] || { echo "Target is not a mounted directory: $TARGET" >&2; exit 1; }
[[ -d "$PACKAGE" ]] || { echo "Package not found: $PACKAGE" >&2; exit 1; }

required=(
    "kernel.elf"
    "titan.elf"
    "TITAN/titan_ul_engine.elf"
    "TITAN/titan_ul_loader.elf"
    "neutrino/neutrino.elf"
)
for relative in "${required[@]}"; do
    [[ -f "$PACKAGE/$relative" ]] || {
        echo "Required package file is missing: $relative" >&2
        exit 1
    }
done

UL_ENGINE_SHA=$(sha256sum "$PACKAGE/TITAN/titan_ul_engine.elf" | awk '{print $1}')
UL_LOADER_SHA=$(sha256sum "$PACKAGE/TITAN/titan_ul_loader.elf" | awk '{print $1}')
[[ "$UL_ENGINE_SHA" == "$UL_LOADER_SHA" ]] || {
    echo "Validated UL compatibility pair diverged: engine=$UL_ENGINE_SHA loader=$UL_LOADER_SHA" >&2
    exit 1
}
echo "UL_COMPATIBILITY_PAIR_SHA256=$UL_ENGINE_SHA"

transaction_root="$TARGET/TITAN/.install-transactions"
mkdir -p "$transaction_root"

recover_journal() {
    local journal=$1
    local kind relative recovery had_existing destination

    echo "Recovering interrupted Titan installation: $journal" >&2
    while IFS='|' read -r kind relative recovery had_existing; do
        [[ "$kind" == FILE || "$kind" == LEGACY ]] || continue
        destination="$TARGET/$relative"

        if [[ -e "$recovery" ]]; then
            rm -f -- "$destination"
            mkdir -p "$(dirname "$destination")"
            mv -f -- "$recovery" "$destination"
        elif [[ "$had_existing" == 0 ]]; then
            rm -f -- "$destination"
        fi
    done < <(tac "$journal")

    rm -f -- "$journal"
    sync
}

# An interrupted earlier run is rolled back before a new transaction begins.
shopt -s nullglob
for pending in "$transaction_root"/*.pending; do
    recover_journal "$pending"
done
shopt -u nullglob

stamp=$(date +%Y%m%d_%H%M%S)
staging="$TARGET/.titan-install-$stamp-$$"
manifest="$staging/INSTALL_SHA256.txt"
journal="$transaction_root/$stamp-$$.pending"
committed=0

rollback_current() {
    local status=$?
    if ((committed == 0)) && [[ -f "$journal" ]]; then
        recover_journal "$journal" || true
    fi
    rm -rf -- "$staging"
    exit "$status"
}
trap rollback_current EXIT INT TERM HUP
mkdir -p "$staging"

# Stage and verify the complete runtime without touching active files.
(
    cd "$PACKAGE"
    find . -type f -print0 | sort -z | while IFS= read -r -d '' relative; do
        relative=${relative#./}
        mkdir -p "$staging/$(dirname "$relative")"
        cp -f -- "$relative" "$staging/$relative"
    done
)
(
    cd "$staging"
    find . -type f ! -name INSTALL_SHA256.txt -print0 |
        sort -z | xargs -0 sha256sum > "$manifest"
    sha256sum -c "$manifest"
)

# Record the complete rollback plan before publishing the first file.
: > "$journal"
while IFS= read -r -d '' staged_file; do
    relative=${staged_file#"$staging/"}
    destination="$TARGET/$relative"
    backup="$destination.titan-backup-$stamp"
    had_existing=0
    [[ -e "$destination" ]] && had_existing=1
    printf 'FILE|%s|%s|%s\n' \
        "$relative" "$backup" "$had_existing" >> "$journal"
done < <(find "$staging" -type f ! -name INSTALL_SHA256.txt -print0 | sort -z)

for legacy_relative in "TITAN/kernel.elf"; do
    legacy="$TARGET/$legacy_relative"
    disabled="$legacy.legacy-disabled-$stamp"
    had_existing=0
    [[ -e "$legacy" ]] && had_existing=1
    printf 'LEGACY|%s|%s|%s\n' \
        "$legacy_relative" "$disabled" "$had_existing" >> "$journal"
done
sync

# Publish each verified file by a rename on the target filesystem.
while IFS= read -r -d '' staged_file; do
    relative=${staged_file#"$staging/"}
    destination="$TARGET/$relative"
    temporary="$destination.titan-new-$stamp"
    backup="$destination.titan-backup-$stamp"

    mkdir -p "$(dirname "$destination")"
    cp -f -- "$staged_file" "$temporary"
    [[ "$(sha256sum "$staged_file" | awk '{print $1}')" == \
       "$(sha256sum "$temporary" | awk '{print $1}')" ]] || {
        echo "Hash mismatch while staging $relative" >&2
        exit 1
    }

    if [[ -e "$destination" ]]; then
        mv -f -- "$destination" "$backup"
    fi
    mv -f -- "$temporary" "$destination"
done < <(find "$staging" -type f ! -name INSTALL_SHA256.txt -print0 | sort -z)

# TITAN/kernel.elf is a stale duplicate of the canonical root kernel path.
# The UL loader compatibility name is validated runtime state and must remain live.
for legacy in "$TARGET/TITAN/kernel.elf"; do
    if [[ -f "$legacy" ]]; then
        mv -f -- "$legacy" "$legacy.legacy-disabled-$stamp"
    fi
done

cp -f -- "$manifest" "$TARGET/TITAN/INSTALL_SHA256.txt"
printf '%s\n' \
    "Project Titan Magenta Edition" \
    "installed_at=$(date --iso-8601=seconds)" \
    "source_package=$PACKAGE" \
    "canonical_kernel=/kernel.elf" \
    "transaction=$stamp-$$" \
    > "$TARGET/TITAN/INSTALL_MANIFEST.txt"

sync
rm -f -- "$journal"
committed=1
rm -rf -- "$staging"
trap - EXIT INT TERM HUP
echo "Project Titan Magenta Edition installed transactionally at: $TARGET"
