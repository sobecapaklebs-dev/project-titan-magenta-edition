set -euo pipefail

test_root=/tmp/project_titan_0_3_90_test
test_binary=/tmp/project_titan_0_3_90_test_runner
kernel_root=$(cd "$(dirname "$0")/.." && pwd)
PROJECT_ROOT=$(cd "$kernel_root/.." && pwd)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# TitanEYE/Ribbon BuildFix26 target-readiness contracts
python3 "$kernel_root/tests/test_buildfix26_target_memory_contract.py" "$kernel_root"
gcc \
    -std=gnu99 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DTITAN_TARGET_MEMORY_TEST \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_target_memory.c" \
    "$kernel_root/tests/test_buildfix26_target_memory_watermarks.c" \
    -o /tmp/project_titan_bf26_target_memory_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf26_target_memory_test

python3 "$kernel_root/tests/test_buildfix26_boot_recovery_contract.py" "$kernel_root"
gcc \
    -std=gnu99 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/graphics" \
    "$kernel_root/src/graphics/titan_boot_recovery.c" \
    "$kernel_root/tests/test_buildfix26_boot_recovery.c" \
    -o /tmp/project_titan_bf26_boot_recovery_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf26_boot_recovery_test
python3 "$kernel_root/tests/test_buildfix26_target_lifecycle_contract.py" "$kernel_root"

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix26_app_preflight_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix26_target_readiness_contract.py" "$PROJECT_ROOT"

# BuildFix27 recovered material boundary through CP10.
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_cp10_recovery_contract.py" "$PROJECT_ROOT"
make -C "$PROJECT_ROOT/apps/wlaunchelf" host-test
make -C "$PROJECT_ROOT/apps/project-nes" host-test

python3 "$kernel_root/tests/test_buildfix27_project_nes_storage_save_contract.py" "$(cd "$kernel_root/.." && pwd)"

python3 "$kernel_root/tests/test_buildfix27_project_nes_conformance_contract.py" "$(cd "$kernel_root/.." && pwd)"

python3 "$kernel_root/tests/test_buildfix27_project_nes_lifecycle_package_contract.py" "$(cd "$kernel_root/.." && pwd)"
python3 "$kernel_root/tests/test_buildfix27_project_nes_provider_contract.py" "$(cd "$kernel_root/.." && pwd)"

gcc -std=c99 -Wall -Wextra -Werror \
    -I"$kernel_root/include" -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_network_client.c" \
    "$kernel_root/tests/test_buildfix27_network_client_host.c" \
    -o /tmp/project_titan_buildfix27_network_client_host
/tmp/project_titan_buildfix27_network_client_host

gcc -std=c99 -Wall -Wextra -Werror \
    -I"$kernel_root/include" \
    "$kernel_root/tests/test_buildfix27_app_sdk_abi.c" \
    -o /tmp/project_titan_buildfix27_app_sdk_abi
/tmp/project_titan_buildfix27_app_sdk_abi

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_wlaunchelf_text_editor_contract.py" "$PROJECT_ROOT"

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_wlaunchelf_recursive_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -DWLE_HOST_TEST \
    -I"$PROJECT_ROOT/apps/wlaunchelf/include" \
    "$PROJECT_ROOT/apps/wlaunchelf/src/wlaunchelf_titan.c" \
    "$PROJECT_ROOT/apps/wlaunchelf/tests/host_recursive_test.c" \
    -o /tmp/project_titan_buildfix27_wle_recursive
ASAN_OPTIONS=detect_leaks=1 /tmp/project_titan_buildfix27_wle_recursive

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_app_image_fit.py"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_wlaunchelf_image_viewer_contract.py" "$PROJECT_ROOT"

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_managed_storage_mount_gate.py" "$PROJECT_ROOT"

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_network_stream_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -Wall -Wextra -Werror -DTITAN_NETWORK_STREAM_TEST \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_network_stream.c" \
    "$kernel_root/tests/test_buildfix27_network_stream_host.c" \
    -o /tmp/project_titan_buildfix27_network_stream_host
/tmp/project_titan_buildfix27_network_stream_host

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_ftp_control_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -Wall -Wextra -Werror \
 -I"$kernel_root/src/services" \
 "$kernel_root/src/services/titan_ftp_service.c" \
 "$kernel_root/tests/test_buildfix27_ftp_control_host.c" \
 -o /tmp/project_titan_buildfix27_ftp_control_host
/tmp/project_titan_buildfix27_ftp_control_host

python3 "$kernel_root/tests/test_buildfix27_lifecycle_classification_closure.py" "$kernel_root"

# BuildFix27 CP24-CP29 closure gates.
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_ftp_data_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -Wall -Wextra -Werror \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_ftp_service.c" \
    "$kernel_root/tests/test_buildfix27_ftp_list_host.c" \
    -o /tmp/project_titan_buildfix27_ftp_list_host
/tmp/project_titan_buildfix27_ftp_list_host
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_ftp_file_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -Wall -Wextra -Werror \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_ftp_service.c" \
    "$kernel_root/tests/test_buildfix27_ftp_file_host.c" \
    -o /tmp/project_titan_buildfix27_ftp_file_host
/tmp/project_titan_buildfix27_ftp_file_host
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_app_ftp_integration_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_titan_app_sdk_docs.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_network_client_browser_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_sas_execution_model_contract.py" "$kernel_root"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_host_readiness_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_fceumm_materialization_gate.py" "$PROJECT_ROOT"

# BuildFix27 CP31 VMC read-only filesystem adapter.
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_vmc_adapter_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_vmc_traversal_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_vmc_export_contract.py" "$PROJECT_ROOT"

# BuildFix27 CP34 host scope closure.
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_scope_closure_contract.py" "$PROJECT_ROOT"

# BuildFix27 CP35 provider-gated PFS/HDD mount lifecycle.
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_storage_mount_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_mount.c" \
    "$kernel_root/tests/test_buildfix27_storage_mount_host.c" \
    -o /tmp/project_titan_buildfix27_storage_mount_host
ASAN_OPTIONS=detect_leaks=1 /tmp/project_titan_buildfix27_storage_mount_host
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_pfs_app_abi_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_wlaunchelf_pfs_ux_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_pfs_provider_boot_contract.py" "$PROJECT_ROOT"
gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$kernel_root/src/services" "$kernel_root/src/services/titan_storage_mount.c" "$kernel_root/src/services/titan_storage_pfs_provider_ps2.c" "$kernel_root/tests/test_buildfix27_pfs_provider_absent_host.c" -o /tmp/project_titan_buildfix27_pfs_provider_absent
ASAN_OPTIONS=detect_leaks=1 /tmp/project_titan_buildfix27_pfs_provider_absent
gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$kernel_root/src/services" "$kernel_root/src/services/titan_storage_mount.c" "$kernel_root/src/services/titan_storage_pfs_provider_ps2.c" "$kernel_root/tests/test_buildfix27_pfs_provider_ps2_host.c" -o /tmp/project_titan_buildfix27_pfs_provider_ps2
ASAN_OPTIONS=detect_leaks=1 /tmp/project_titan_buildfix27_pfs_provider_ps2

python3 "$kernel_root/tests/test_buildfix27_pfs_iop_provenance_contract.py" "$kernel_root/.."
python3 "$kernel_root/tests/test_buildfix27_pfs_iop_loader_contract.py" "$kernel_root/.."

python3 "$kernel_root/tests/test_buildfix27_fceumm_importer_contract.py" "$kernel_root/.."
python3 "$kernel_root/tests/test_buildfix27_fceumm_importer_host.py" "$kernel_root/.."

python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_ps2dev_bundle_intake_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_project_nes_fceumm_adapter_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_target_orchestrator_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_hardware_evidence_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_buildfix27_rehydrate_contract.py" "$PROJECT_ROOT"
python3 "$SCRIPT_DIR/test_buildfix28_external_session_contract.py"
gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix28_platform_overhead.c" \
    "$kernel_root/src/services/titan_platform_overhead.c" \
    "$kernel_root/src/services/titan_time.c" \
    -o /tmp/project_titan_buildfix28_overhead
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix28_overhead
python3 "$SCRIPT_DIR/test_buildfix28_fast_paths_contract.py"
python3 "$SCRIPT_DIR/test_buildfix28_prefreeze_contract.py"
python3 "$SCRIPT_DIR/test_buildfix28_theme_v4_schema_contract.py"
gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$kernel_root/src/services" "$kernel_root/tests/test_buildfix28_theme_v4_runtime.c" "$kernel_root/src/services/titan_theme_v4_runtime.c" -o /tmp/project_titan_buildfix28_theme_v4_runtime
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix28_theme_v4_runtime
python3 "$SCRIPT_DIR/test_buildfix28_theme_v4_runtime_contract.py"
python3 "$SCRIPT_DIR/test_buildfix28_theme_v4_renderer_contract.py" "$PROJECT_ROOT"
gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix28_theme_v4_rare_event.c" \
    "$kernel_root/src/services/titan_theme_v4_runtime.c" \
    -o /tmp/project_titan_buildfix28_theme_v4_rare_event
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix28_theme_v4_rare_event
python3 "$SCRIPT_DIR/test_buildfix28_theme_v4_profiler_contract.py" "$PROJECT_ROOT"
python3 "$SCRIPT_DIR/test_buildfix28_no_frame_allocation_contract.py" "$PROJECT_ROOT"
python3 "$SCRIPT_DIR/test_buildfix28_neutrino_overlay_contract.py"
python3 "$SCRIPT_DIR/test_buildfix28_neutrino_overlay_resume_contract.py"
python3 "$SCRIPT_DIR/test_buildfix28_neutrino_return_contract.py"
python3 "$SCRIPT_DIR/test_buildfix28_theme_v4_flagship_contract.py" "$PROJECT_ROOT"
gcc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix28_theme_v4_flagship.c" \
    "$kernel_root/src/services/titan_theme_v4_runtime.c" \
    -o /tmp/project_titan_buildfix28_theme_v4_flagship
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix28_theme_v4_flagship "$kernel_root/tests/fixtures/theme_v4_flagship/vice_city_flagship.compiled"
python3 "$SCRIPT_DIR/test_buildfix28_lifecycle_recovery_contract.py" "$PROJECT_ROOT"
