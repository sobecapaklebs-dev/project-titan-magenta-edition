set -euo pipefail

test_root=/tmp/project_titan_0_3_90_test
test_binary=/tmp/project_titan_0_3_90_test_runner
kernel_root=$(cd "$(dirname "$0")/.." && pwd)
PROJECT_ROOT=$(cd "$kernel_root/.." && pwd)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/platform/ps2" \
    "$kernel_root/src/services/titan_game_runtime.c" \
    "$kernel_root/tests/test_titan_game_runtime.c" \
    -o /tmp/project_titan_game_runtime_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_game_runtime_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/graphics" \
    "$kernel_root/src/graphics/titan_video_game_texture.c" \
    "$kernel_root/tests/test_titan_game_renderer.c" \
    -lm \
    -o /tmp/project_titan_game_renderer_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_game_renderer_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/graphics/titan_pikavr.c" \
    "$kernel_root/tests/test_titan_pikavr.c" \
    -o /tmp/project_titan_pikavr_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_pikavr_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_elf_validate.c" \
    "$kernel_root/src/services/titan_sas.c" \
    "$kernel_root/src/services/titan_applications.c" \
    "$kernel_root/tests/test_app_platform_stubs.c" \
    "$kernel_root/src/services/titan_software_badges.c" \
    "$kernel_root/tests/test_titan_software_badges.c" \
    -o /tmp/project_titan_software_badges_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_software_badges_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_vlombiscore.c" \
    "$kernel_root/tests/test_titan_vlombiscore.c" \
    -o /tmp/project_titan_vlombiscore_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_vlombiscore_test
python3 "$kernel_root/tests/test_vlombis_scheduler_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_diagnostics_categories_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_package_installer_ui_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_ui_bounds_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_theme_context_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_theme_transition_crossfade_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_titan_game_runtime_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_titaneye_port_contract.py" "$kernel_root/.."
python3 "$kernel_root/tests/test_ribbon_reference_contract.py" "$kernel_root"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/include" \
    -I"$kernel_root/../ports/goldeneye" \
    "$kernel_root/../ports/goldeneye/titan_ge_platform.c" \
    "$kernel_root/tests/test_titan_ge_platform.c" \
    -o /tmp/project_titan_ge_platform_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_ge_platform_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/include" \
    -I"$kernel_root/../ports/goldeneye" \
    "$kernel_root/../ports/goldeneye/titan_ge_rom.c" \
    "$kernel_root/tests/test_titan_ge_rom.c" \
    -o /tmp/project_titan_ge_rom_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_ge_rom_test

python3 "$kernel_root/tests/test_sas_execution_model_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix22_resolution_vram_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix22_sdk_capability_docs.py" "$kernel_root"

# BuildFix23 CP02: 720p composition planner keeps internal and output resolution separate.
gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/src/graphics" \
    "$kernel_root/src/graphics/titan_video_720p_plan.c" \
    "$kernel_root/tests/test_buildfix23_960x540_internal.c" \
    -o /tmp/project_titan_bf23_960_plan_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf23_960_plan_test
python3 "$kernel_root/tests/test_buildfix23_internal_output_resolution_contract.py" "$kernel_root"

# BuildFix23 CP03: deterministic 960x540 -> 1280x720 scaling and logical UI/text geometry.
python3 "$kernel_root/tests/test_buildfix23_720p_scaling_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_720p_text_legibility_contract.py" "$kernel_root"

# BuildFix23 CP05-CP06 release-polish contracts
python3 "$kernel_root/tests/test_buildfix23_720p_vram_headroom_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_transition_compositor_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_ui_focus_transition_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_text_bounds_cache_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_dex_overlay_safe_area_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_collision_detector_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_rapid_input_focus_contract.py" "$kernel_root"

# BuildFix23 CP07 surface-layout contracts
python3 "$kernel_root/tests/test_buildfix23_package_list_row_layout_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix23_applications_library_bounds_contract.py" "$kernel_root"

# BuildFix24 SAS normative contracts
python3 "$kernel_root/tests/test_buildfix24_sas_reference_audit_contract.py" "$(dirname "$kernel_root")"
python3 "$kernel_root/tests/test_buildfix24_sas_compliance_model_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_compliance_implies_compatibility_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_compatible_noncompliant_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_invalid_compliance_state_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_no_install_pbt_requirement_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_prefix_recognition_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_classic_osdsys_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_extended_subfolder_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_copyability_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_titlecfg_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_appcfg_fallback_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_optional_files_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_archive_import_contract.py" "$(dirname "$kernel_root")"
python3 "$kernel_root/tests/test_buildfix24_sas_library_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_badge_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix24_sas_validator_contract.py" "$(dirname "$kernel_root")"
python3 "$kernel_root/tests/test_buildfix24_sas_sdk_freeze_contract.py" "$(dirname "$kernel_root")"


# TitanEYE/Ribbon BuildFix25 hardening contracts
for test in \
    test_buildfix25_480i_validation_selector_isolation_contract.py \
    test_buildfix25_720p_double_failure_contract.py \
    test_buildfix25_720p_invalid_preference_contract.py \
    test_buildfix25_720p_release_reference_default_contract.py \
    test_buildfix25_720p_rollback_initialized_state_contract.py \
    test_buildfix25_720p_transactional_reconfigure_contract.py \
    test_buildfix25_boot_resource_lifetime_contract.py \
    test_buildfix25_boot_vram_budget_contract.py \
    test_buildfix25_boot_vram_history_export_contract.py \
    test_buildfix25_boot_vram_phase_contract.py \
    test_buildfix25_pikavr_startup_accounting_contract.py \
    test_buildfix25_sas_lazy_vram_contract.py \
    test_buildfix25_theme_boot_degradation_contract.py \
    test_buildfix25_vram_accounting_overflow_contract.py \
    test_buildfix25_vram_allocation_failure_contract.py \
    test_buildfix25_vram_critical_fallback_contract.py \
    test_buildfix25_vram_fault_injection_contract.py \
    test_buildfix25_vram_reserved_headroom_contract.py
do
    python3 "$kernel_root/tests/$test" "$kernel_root"
done
python3 "$kernel_root/tests/test_buildfix25_first_hardware_profile_contract.py" "$(dirname "$kernel_root")"

gcc \
    -std=gnu99 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DTITAN_VRAM_FAULT_INJECTION \
    -I"$kernel_root/include" -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix25_vram_fault_injection.c" \
    "$kernel_root/src/graphics/titan_vram_boot_guard.c" \
    "$kernel_root/src/graphics/titan_boot_recovery.c" \
    "$kernel_root/src/services/titan_target_memory.c" \
    -o /tmp/project_titan_bf25_vram_fault_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf25_vram_fault_test

gcc \
    -std=gnu99 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/include" -I"$kernel_root/src/core" -I"$kernel_root/src/graphics" \
    "$kernel_root/tests/test_buildfix25_720p_transaction_resolution.c" \
    "$kernel_root/src/graphics/titan_video_720p_transaction.c" \
    -o /tmp/project_titan_bf25_720p_transaction_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf25_720p_transaction_test

