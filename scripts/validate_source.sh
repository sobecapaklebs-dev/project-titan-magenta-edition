#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

export LC_ALL=C

if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$ROOT" diff --check
fi
bash -n scripts/*.sh

# Lock the Titan UL build-environment contract: GSKIT must be auto-detected
# from a normal PS2DEV layout even when the caller did not export it.
titan_env_test=$(mktemp -d "${TMPDIR:-/tmp}/titan-gskit-env.XXXXXX")
trap 'rm -rf "$titan_env_test"' EXIT
mkdir -p \
    "$titan_env_test/ps2dev/gsKit/include" \
    "$titan_env_test/ps2dev/gsKit/lib" \
    "$titan_env_test/ps2dev/ps2sdk"
: > "$titan_env_test/ps2dev/gsKit/include/gsKit.h"
: > "$titan_env_test/ps2dev/gsKit/lib/libgskit.a"
: > "$titan_env_test/ps2dev/gsKit/lib/libdmakit.a"
env -u GSKIT \
    PS2DEV="$titan_env_test/ps2dev" \
    PS2SDK="$titan_env_test/ps2dev/ps2sdk" \
    scripts/build_titan_ul_engine.sh --check-env |
    grep -Fqx "GSKIT=$titan_env_test/ps2dev/gsKit"
rm -rf "$titan_env_test"
trap - EXIT

bash kernel/tests/run_host_tests.sh
"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -Ibootloader/src \
    bootloader/tests/test_boot_path.c bootloader/src/boot_path.c \
    -o /tmp/project_titan_boot_path_test
/tmp/project_titan_boot_path_test

"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -Ibootloader/src -Icommon \
    bootloader/tests/test_elf_validate.c bootloader/src/elf_validate.c \
    -o /tmp/project_titan_elf_validate_test
/tmp/project_titan_elf_validate_test

gcc -std=c11 -Wall -Wextra -Werror \
    -Imodules/titan-neutrino-engine/ee/loader/include \
    -Imodules/titan-neutrino-engine/ee/loader/src \
    -c modules/titan-neutrino-engine/ee/loader/src/ul_chain.c \
    -o /tmp/project_titan_ul_chain_host.o

scripts/test_installer.sh
python3 kernel/tools/verify_curated_system_assets.py --root kernel/assets/ui
python3 kernel/tools/generate_system_assets.py --output /tmp/project_titan_generated_assets
test -s /tmp/project_titan_generated_assets/titan_ui_icons.a8
test -s /tmp/project_titan_generated_assets/titan_ui_avatars.psm16
rm -rf /tmp/project_titan_generated_assets /tmp/project_titan_ul_chain_host.o \
    /tmp/project_titan_boot_path_test /tmp/project_titan_elf_validate_test

# Titan UL headless recovery uses PS2SDK debug-screen helpers. Keep libdebug
# linked so init_scr/scr_printf cannot regress into an opl.elf link failure.
grep -F 'titanULRecoverFromFailure' "$ROOT/modules/titan-ul-engine/src/opl.c" >/dev/null
grep -E '^EE_LIBS .* -ldebug([[:space:]]|$)' "$ROOT/modules/titan-ul-engine/Makefile" >/dev/null

printf 'project-titan-source-validation: OK\n'

# P162 artwork closure gate
python3 kernel/tests/test_p162_rebased_artwork_contract.py .
