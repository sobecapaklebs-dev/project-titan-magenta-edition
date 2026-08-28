#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)

# Diagnostic target sweep: preserve the normal release build as fail-fast, but
# provide an explicit mode that keeps independent target stages moving so one
# invocation can expose multiple compiler/linker blockers.
if [[ ${1:-} == --diagnostic-sweep ]]; then
    shift
    exec bash "$ROOT/scripts/build_diagnostic_sweep.sh" "$@"
fi

JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
VERIFY_REPRODUCIBLE=0
[[ ${1:-} == --verify-reproducible ]] && VERIFY_REPRODUCIBLE=1

ROOT_REAL=$(cd "$ROOT" && pwd -P)
PROJECT_GIT_ROOT=""
git_root_candidate=$(git -C "$ROOT" rev-parse --show-toplevel 2>/dev/null || true)
if [[ -n "$git_root_candidate" ]]; then
    git_root_candidate=$(cd "$git_root_candidate" && pwd -P)
    if [[ "$git_root_candidate" == "$ROOT_REAL" ]]; then
        PROJECT_GIT_ROOT=$git_root_candidate
    fi
fi

if [[ -n "$PROJECT_GIT_ROOT" ]]; then
    TITAN_SOURCE_REVISION=$(git -C "$PROJECT_GIT_ROOT" rev-parse HEAD)
    TITAN_SOURCE_EPOCH=$(git -C "$PROJECT_GIT_ROOT" log -1 --format=%ct)
else
    TITAN_SOURCE_REVISION="source-archive-$(cat "$ROOT/VERSION")"
    TITAN_SOURCE_EPOCH=$(stat -c %Y "$ROOT/VERSION" 2>/dev/null || date +%s)
fi

snapshot_source_tree() {
    local destination=$1

    # Snapshot the source tree the user is actually building, not merely HEAD.
    # This is important when a release archive/hotfix is unpacked over an
    # existing Git checkout: git archive HEAD silently drops modified and
    # untracked release files (for example bootloader/include/titan_boot_version.h).
    # Generated build products are still excluded so every build starts clean.
    tar -C "$ROOT" \
        --exclude='./.git' \
        --exclude='./build' \
        --exclude='./dist' \
        --exclude='./bootloader/generated' \
        --exclude='./bootloader/stage2/generated' \
        --exclude='./kernel/src/graphics/generated' \
        --exclude='./kernel/src/services/generated' \
        --exclude='./apps/project-nes/build' \
        --exclude='./apps/wlaunchelf/build' \
        --exclude='./ports/goldeneye/build' \
        --exclude='__pycache__' \
        --exclude='*.pyc' \
        --exclude='*.o' \
        --exclude='*.map' \
        --exclude='*.bak' \
        --exclude='*.orig' \
        --exclude='*.rej' \
        -cf - . | tar -C "$destination" -xf -
}

[[ -n "${PS2DEV:-}" && -n "${PS2SDK:-}" ]] || {
    echo "PS2DEV and PS2SDK must be exported." >&2
    exit 1
}
for required_tool in \
    "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" \
    "$PS2DEV/iop/bin/mipsel-none-elf-gcc" \
    "$PS2DEV/dvp/bin/dvp-as" \
    "$PS2DEV/dvp/bin/dvp-objcopy" \
    "$PS2SDK/bin/bin2c"; do
    [[ -x "$required_tool" ]] || {
        echo "Required PS2 tool is missing: $required_tool" >&2
        exit 1
    }
done
python3 -c 'from PIL import Image' || {
    echo "Python Pillow is required for deterministic asset validation." >&2
    exit 1
}

# Fail before the expensive host suite if the target environment cannot
# possibly complete the release build.  This also catches stale PS2DEV paths
# and renamed/missing PS2SDK IRXs deterministically.
python3 "$ROOT/tools/titan_target_preflight.py"

lock_value() {
    awk -F= -v key="$1" '$1 == key {print $2; exit}' "$ROOT/TOOLCHAIN.lock"
}
verify_repo_commit() {
    local repository=$1 expected=$2 label=$3 actual
    [[ -n "$expected" ]] || return 0
    if git -C "$repository" rev-parse HEAD >/dev/null 2>&1; then
        actual=$(git -C "$repository" rev-parse HEAD)
        [[ "$actual" == "$expected" ]] || {
            echo "$label commit mismatch: expected $expected, found $actual" >&2
            exit 1
        }
    else
        echo "WARNING: cannot verify $label commit at $repository" >&2
    fi
}
verify_repo_commit "$PS2DEV" "$(lock_value ps2dev_repository_commit)" ps2dev
verify_repo_commit "$PS2DEV/build/ps2toolchain" "$(lock_value ps2toolchain_repository_commit)" ps2toolchain
verify_repo_commit "$PS2DEV/build/ps2sdk" "$(lock_value ps2sdk_repository_commit)" ps2sdk

export LC_ALL=C TZ=UTC
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$TITAN_SOURCE_EPOCH}

build_once() (
    local output=$1
    local work
    work=$(mktemp -d "${TMPDIR:-/tmp}/titan-build.XXXXXX")
    trap 'rm -rf "$work"' EXIT

    # A clean copy prevents stale objects and generated files from influencing
    # the result in both a Git checkout and an extracted release ZIP.
    snapshot_source_tree "$work"
    mkdir -p "$output"

    # build_all.sh produces a target/runtime distribution, not a public
    # source-clean archive. Preserve every host/source contract except the
    # source-release payload cleanliness gate, which intentionally rejects the
    # ELF/IRX artifacts present in a hardware build tree.
    TITAN_SKIP_SOURCE_RELEASE_AUDIT=1 bash "$work/scripts/validate_source.sh"
    make -C "$work/bootloader" clean
    make -C "$work/bootloader" clean-generated
    make -C "$work/bootloader" all

    # P6 / Early Input binary freshness gate: the source snapshot intentionally
    # keeps hardware-build IRX artifacts, so a pre-existing bt_pademu.irx or
    # usb_pademu.irx must never be trusted as current.  The kernel embeds these
    # files with bin2c before Titan UL is built later in this script.  Rebuild
    # both transports now from the exact snapshot sources so EE Early Input and
    # the IOP PADEMU protocol/telemetry are always from the same revision.
    make -C "$work/modules/titan-ul-engine/modules/pademu" clean USE_USB=1
    make -C "$work/modules/titan-ul-engine/modules/pademu" clean USE_BT=1
    make -C "$work/modules/titan-ul-engine/modules/pademu" USE_USB=1
    make -C "$work/modules/titan-ul-engine/modules/pademu" USE_BT=1

    make -C "$work/kernel" clean
    make -C "$work/kernel" clean-generated
    make -C "$work/kernel" all
    # P10.4: prove the exact freshly rebuilt BT IRX survived bin2c and is
    # present byte-for-byte in the kernel that will be copied to dist.
    python3 "$work/kernel/tools/verify_pademu_embed_identity.py" "$work"

    # First-party Titan App proof package.  It must be compiled for the real
    # EE/R5900 target on the same PS2DEV installation as the Titan kernel.
    make -C "$work/apps/project-nes" clean
    make -C "$work/apps/project-nes" host-test
    make -C "$work/apps/project-nes" all verify \
        EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"

    # First native utility port: wLaunchELF runs as a Titan .tmod and consumes
    # the kernel-owned storage/ELF-handoff API rather than loading its own IOP
    # stack or replacing the shell renderer/input loop.
    make -C "$work/apps/wlaunchelf" clean
    make -C "$work/apps/wlaunchelf" host-test
    make -C "$work/apps/wlaunchelf" all verify \
        EE_CC="$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc"

    make -C "$work/modules/titan-neutrino-engine" clean \
        GIT_TAG="titan-$(cat "$work/VERSION")"
    make -C "$work/modules/titan-neutrino-engine" -j"$JOBS" all \
        GIT_TAG="titan-$(cat "$work/VERSION")"
    make -C "$work/modules/titan-neutrino-engine/ee/loader" clean all DEBUG=0 \
        GIT_TAG="titan-$(cat "$work/VERSION")"
    # The loader's clean target intentionally removes ee/loader/modules.
    # Collect the IRX payload only after the final loader rebuild so the
    # disposable release package cannot lose its Neutrino module directory.
    make -C "$work/modules/titan-neutrino-engine" copy

    TITAN_SOURCE_COMMIT="$TITAN_SOURCE_REVISION" \
        JOBS="$JOBS" bash "$work/scripts/build_titan_ul_engine.sh"

    local neutrino_loader="$work/modules/titan-neutrino-engine/ee/loader"
    [[ -f "$neutrino_loader/neutrino.elf" ]] || {
        echo "Titan Neutrino Engine artifact missing: neutrino.elf" >&2
        exit 1
    }
    [[ -d "$neutrino_loader/config" ]] || {
        echo "Titan Neutrino Engine artifact missing: config directory" >&2
        exit 1
    }
    [[ -d "$neutrino_loader/modules" ]] || {
        echo "Titan Neutrino Engine artifact missing: modules directory" >&2
        exit 1
    }
    compgen -G "$neutrino_loader/modules/*.irx" >/dev/null || {
        echo "Titan Neutrino Engine modules directory contains no IRX files" >&2
        exit 1
    }

    mkdir -p "$output/TITAN/audio" "$output/TITAN/packages" "$output/neutrino"
    cp "$work/bootloader/BOOT.ELF" "$output/BOOT.ELF"
    cp "$work/kernel/kernel.elf" "$output/kernel.elf"
    cp "$neutrino_loader/neutrino.elf" "$output/neutrino/neutrino.elf"
    cp -R "$neutrino_loader/config" "$output/neutrino/"
    cp -R "$neutrino_loader/modules" "$output/neutrino/"
    cp "$work/build/modules/titan-ul-engine/OPNPS2LD.ELF" \
        "$output/TITAN/titan_ul_engine.elf"
    cp -R "$work/runtime/TITAN/audio/." "$output/TITAN/audio/"
    cp "$work/apps/project-nes/build/PROJECTNES.pkg" \
        "$output/TITAN/packages/PROJECTNES.pkg"
    cp "$work/apps/wlaunchelf/build/WLAUNCHELF.pkg" \
        "$output/TITAN/packages/WLAUNCHELF.pkg"

    {
        echo "project=Project Titan Magenta Edition"
        echo "version=$(cat "$work/VERSION")"
        echo "source_commit=$TITAN_SOURCE_REVISION"
        echo "source_date_epoch=$SOURCE_DATE_EPOCH"
        echo "ps2dev=${PS2DEV}"
        echo "ps2sdk=${PS2SDK}"
        echo "bt_pademu_sha256=$(sha256sum "$work/modules/titan-ul-engine/modules/pademu/bt_pademu.irx" | awk '{print $1}')"
        echo "bt_pademu_size=$(stat -c %s "$work/modules/titan-ul-engine/modules/pademu/bt_pademu.irx")"
        "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" --version | head -n 1
        find "$output" -type f ! -name BUILD_MANIFEST.txt -print0 |
            sort -z | xargs -0 sha256sum
    } > "$output/BUILD_MANIFEST.txt"

)

rm -rf "$ROOT/build/repro-a" "$ROOT/build/repro-b" "$ROOT/dist/Project Titan Magenta Edition"
build_once "$ROOT/build/repro-a"

if ((VERIFY_REPRODUCIBLE)); then
    build_once "$ROOT/build/repro-b"
    diff -u \
        <(cd "$ROOT/build/repro-a" && find . -type f ! -name BUILD_MANIFEST.txt -print0 | sort -z | xargs -0 sha256sum) \
        <(cd "$ROOT/build/repro-b" && find . -type f ! -name BUILD_MANIFEST.txt -print0 | sort -z | xargs -0 sha256sum)
fi

mkdir -p "$ROOT/dist"
cp -R "$ROOT/build/repro-a" "$ROOT/dist/Project Titan Magenta Edition"
echo "Runtime package: $ROOT/dist/Project Titan Magenta Edition"
