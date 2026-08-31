#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$ROOT"

EXPECTED_RUNNER=304b98f5d24c8c45a3aebf8c90a6d908416034a949f48338ae9dee6ac758e16e
EXPECTED_BUILD_ALL=aef6e867d321d6bc286527317568ce473ce2d8f432d89837a199a9a3d8ab644e

[[ "$(sha256sum kernel/tests/run_host_tests.sh | awk '{print $1}')" == "$EXPECTED_RUNNER" ]] || {
  echo "ERRO: run_host_tests.sh foi alterado; R4 nao mascara gates." >&2; exit 1; }
[[ "$(sha256sum scripts/build_all.sh | awk '{print $1}')" == "$EXPECTED_BUILD_ALL" ]] || {
  echo "ERRO: build_all.sh difere do source(6) original; recuso validar sobre wrapper contaminado." >&2; exit 1; }

python3 kernel/tests/test_game_patch_golden_contract.py "$ROOT"

TMP="$(mktemp -d /tmp/titan-gamepatch-r4-host.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ikernel/src/services \
  kernel/tests/test_titan_game_patches_host.c \
  kernel/src/services/titan_game_patches.c \
  -o "$TMP/gamepatch"
(
  cd "$TMP"
  ASAN_OPTIONS=detect_leaks=1 ./gamepatch
)

# Historical Neutrino/UL launch contract with the exact original host link set.
gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ikernel/include -Ikernel/src/core -Ikernel/src/services \
  kernel/src/services/titan_shell_busy.c \
  kernel/src/services/titan_storage_io.c \
  kernel/src/services/titan_session_service.c \
  kernel/src/services/titan_time.c \
  kernel/src/services/titan_neutrino.c \
  kernel/tests/test_neutrino_catalog.c \
  -o "$TMP/neutrino"
(
  cd "$TMP"
  ASAN_OPTIONS=detect_leaks=0 ./neutrino
)

echo "GAME_PATCH_R4_HOST_VALIDATION=PASS"
echo "FULL_TEST_RUNNER_MODIFIED=NO"
echo "BUILD_ALL_MODIFIED=NO"
