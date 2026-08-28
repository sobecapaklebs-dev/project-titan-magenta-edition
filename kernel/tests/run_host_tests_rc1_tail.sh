#!/usr/bin/env bash
set -euo pipefail
kernel_root=$(cd "$(dirname "$0")/.." && pwd)
PROJECT_ROOT=$(cd "$kernel_root/.." && pwd)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# BuildFix29 CP08 — Titan Repository Protocol v1.
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix29_repository_protocol.c" \
    "$kernel_root/src/services/titan_repository_protocol.c" \
    -o /tmp/project_titan_buildfix29_repository_protocol
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix29_repository_protocol "$kernel_root/tests/fixtures/repository_v1/catalog.trepo"
python3 "$kernel_root/tests/test_buildfix29_repository_protocol_contract.py" "$PROJECT_ROOT"


# BuildFix29 CP09 — Titan Download Service integrity/queue/storage pipeline.
gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DTITAN_DOWNLOAD_SERVICE_HOST_TEST \
    -I"$kernel_root/src/services" -I"$PROJECT_ROOT/kernel/include" \
    "$kernel_root/tests/test_buildfix29_download_service.c" \
    "$kernel_root/src/services/titan_download_service.c" \
    "$kernel_root/src/services/titan_sha256.c" \
    -o /tmp/project_titan_buildfix29_download_service
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix29_download_service
python3 "$kernel_root/tests/test_buildfix29_download_service_contract.py" "$PROJECT_ROOT"


# BuildFix29 CP10 — Titan Store Foundation / Browser package handoff.
gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DTITAN_STORE_SERVICE_HOST_TEST -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix29_store_foundation.c" \
    "$kernel_root/src/services/titan_store_service.c" \
    "$kernel_root/src/services/titan_repository_protocol.c" \
    -o /tmp/project_titan_buildfix29_store_foundation
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix29_store_foundation
python3 "$kernel_root/tests/test_buildfix29_store_foundation_contract.py" "$PROJECT_ROOT"

# BuildFix29 CP11 — platform closure / lazy idle-cost gates.
python3 "$kernel_root/tests/test_buildfix29_platform_closure_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix29_platform_input_closure_contract.py" "$PROJECT_ROOT"

# BuildFix30 — Date/Time/RTC/Internet Time release-candidate contracts.
cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -I"$SCRIPT_DIR/../src/services" \
  "$SCRIPT_DIR/test_buildfix30_datetime.c" \
  "$SCRIPT_DIR/../src/services/titan_datetime.c" \
  -Wl,--gc-sections -o "$SCRIPT_DIR/.buildfix30_datetime_host"
"$SCRIPT_DIR/.buildfix30_datetime_host"
rm -f "$SCRIPT_DIR/.buildfix30_datetime_host"
python3 "$SCRIPT_DIR/test_buildfix30_datetime_contracts.py"

# BuildFix30 Theme Dynamic Lifecycle finalization
cc -std=c11 -Wall -Wextra -Werror -Ikernel/src/services \
  kernel/tests/test_buildfix30_theme_lifecycle.c kernel/src/services/titan_theme_lifecycle.c \
  -o /tmp/titan_bf30_theme_lifecycle_test
/tmp/titan_bf30_theme_lifecycle_test
python3 kernel/tests/test_buildfix30_theme_lifecycle_contracts.py

# BuildFix30 Titan Disc Service identification/state machine
cc -std=gnu11 -Wall -Wextra -Werror -DTITAN_DISC_HOST_TEST -Ikernel/src/services \
  kernel/tests/test_buildfix30_disc_service.c kernel/src/services/titan_disc_service.c kernel/src/services/titan_theme_lifecycle.c \
  -D'__attribute__(x)=' -Wno-unused-function -o /tmp/titan_bf30_disc_service_test
/tmp/titan_bf30_disc_service_test
python3 kernel/tests/test_buildfix30_disc_service_contract.py

# BuildFix30 Titan Disc Engine / Session / HOME / Smart IGR integration
cc -std=gnu99 -Wall -Wextra -Werror \
  -I"$PROJECT_ROOT/kernel/include" -I"$kernel_root/src/core" -I"$kernel_root/src/services" \
  "$kernel_root/src/services/titan_disc_engine.c" \
  "$kernel_root/tests/test_buildfix30_disc_engine.c" \
  -o /tmp/titan_bf30_disc_engine_test
/tmp/titan_bf30_disc_engine_test
python3 "$kernel_root/tests/test_buildfix30_disc_engine_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix30_disc_return_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix30_disc_shutdown_contract.py" "$PROJECT_ROOT"

# BuildFix30 Global Language / Text / UX consistency architecture
cc -std=c11 -Wall -Wextra -Werror \
  -I"$PROJECT_ROOT/kernel/include" -I"$kernel_root/src/services" -I"$kernel_root/src/core" -I"$kernel_root/src/ui" -I"$kernel_root/src/graphics" \
  "$kernel_root/src/services/titan_localization.c" "$kernel_root/tests/test_buildfix30_localization.c" \
  -o /tmp/titan_bf30_localization_test
/tmp/titan_bf30_localization_test
python3 "$kernel_root/tests/test_buildfix30_localization_contract.py"

# BuildFix30 validated boot baseline / TINS release packaging hardening
python3 "$kernel_root/tests/test_buildfix30_boot_packaging_contract.py"

# BuildFix30 PUP transaction / auto-reboot / power teardown finalization
python3 "$kernel_root/tests/test_buildfix30_pup_power_contract.py"

# BuildFix30 Store/Repository release revalidation + public SDK/API v1 freeze
python3 "$kernel_root/tests/test_buildfix30_api_freeze_contract.py"

# BuildFix30 release-regression meta gate
python3 "$kernel_root/tests/test_buildfix30_release_regression_contract.py"

# BF30 FMCB release-blocker closure
python3 "$kernel_root/tests/test_buildfix30_fmcb_release_blocker_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/tests/host_stubs" -I"$kernel_root/src/installer" \
    "$kernel_root/tests/test_buildfix30_fmcb_rom_contract.c" "$kernel_root/src/installer/titan_installer_model.c" \
    -o /tmp/project_titan_bf30_fmcb_rom_contract
ASAN_OPTIONS=detect_leaks=1 /tmp/project_titan_bf30_fmcb_rom_contract

# BF30 target-source static audit: closes gaps only exposed by real PS2 compilation.
python3 "$kernel_root/tests/test_buildfix30_target_source_audit_contract.py" "$PROJECT_ROOT"

# BF30 real-hardware feedback: restore approved 720p/Flow performance and input/BT UX.
python3 "$kernel_root/tests/test_bf30_hwfix2_performance_regression_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_bf30_hwfix2_input_bt_welcome_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_bf30_hwfix2_perf_tech_observability_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_bf30_vlombis_full_integration_contract.py" "$kernel_root"
