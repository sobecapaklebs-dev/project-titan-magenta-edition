#!/usr/bin/env bash
set -euo pipefail

test_root=/tmp/project_titan_0_3_90_test
test_binary=/tmp/project_titan_0_3_90_test_runner
kernel_root=$(cd "$(dirname "$0")/.." && pwd)
PROJECT_ROOT=$(cd "$kernel_root/.." && pwd)
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

rm -rf "$test_root"
mkdir -p "$test_root"

# Artwork cache/renderer regressions: OPL format compatibility, stable visible
# icon residency, and reloading the single resident background on selection.
python3 "$kernel_root/tests/test_external_opl_art_direct_contract.py"
python3 "$kernel_root/tests/test_dex_operation_ribbon_contract.py"
python3 "$kernel_root/tests/test_asset_service_syntax.py"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_0_3_90_test/TITAN"' \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_user_profile.c" \
    "$kernel_root/src/services/titan_profile_snapshot.c" \
    "$kernel_root/src/services/titan_profile_storage.c" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_shell_busy.c" \
    "$kernel_root/src/services/titan_vmc.c" \
    "$kernel_root/tests/test_profile_storage_vmc.c" \
    -o "$test_binary"

ASAN_OPTIONS=detect_leaks=0 "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary" recover
ASAN_OPTIONS=detect_leaks=0 "$test_binary" corrupt

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/tests/test_storage_io_fallback.c" \
    -o /tmp/project_titan_0_3_90_storage_fallback_test

ASAN_OPTIONS=detect_leaks=0 \
    /tmp/project_titan_0_3_90_storage_fallback_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_shell_busy.c" \
    "$kernel_root/tests/test_shell_busy.c" \
    -o /tmp/project_titan_0_3_90_shell_busy_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_shell_busy_test

gcc \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_game_gsm.c" \
    "$kernel_root/tests/test_game_gsm_persistence.c" \
    -o /tmp/project_titan_game_gsm_persistence_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_game_gsm_persistence_test
python3 -S "$kernel_root/tests/test_game_gsm_per_game_contract.py" "$PROJECT_ROOT"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_shell_busy.c" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_session_service.c" \
    "$kernel_root/src/services/titan_time.c" \
    "$kernel_root/src/services/titan_neutrino.c" \
    "$kernel_root/tests/test_neutrino_catalog.c" \
    -o /tmp/project_titan_0_3_90_neutrino_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_neutrino_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_elf_validate.c" \
    "$kernel_root/src/services/titan_sas.c" \
    "$kernel_root/src/services/titan_applications.c" \
    "$kernel_root/tests/test_app_platform_stubs.c" \
    "$kernel_root/tests/test_applications.c" \
    "$kernel_root/tests/test_localization_compat_stub.c" \
    -o /tmp/project_titan_0_3_90_applications_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_applications_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_STORAGE_IO_TESTING \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_package_installer_test/TITAN"' \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_elf_validate.c" \
    "$kernel_root/src/services/titan_sas.c" \
    "$kernel_root/src/services/titan_applications.c" \
    "$kernel_root/tests/test_app_platform_stubs.c" \
    "$kernel_root/src/services/titan_theme.c" \
    "$kernel_root/src/services/titan_package_installer.c" \
    "$kernel_root/tests/test_package_installer.c" \
    "$kernel_root/tests/test_localization_compat_stub.c" \
    -o /tmp/project_titan_0_3_90_package_installer_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_package_installer_test

python3 "$kernel_root/tests/test_titan_pkg.py"

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
    -I"$kernel_root/src/platform/ps2" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/ui" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_storage_mount.c" \
    "$kernel_root/src/services/titan_session_service.c" \
    "$kernel_root/src/services/titan_igr_compatibility.c" \
    "$kernel_root/src/services/titan_ingame_overlay.c" \
    "$kernel_root/src/services/titan_unified_input.c" \
    "$kernel_root/src/services/titan_platform_overhead.c" \
    "$kernel_root/src/services/titan_app_runtime.c" \
    "$kernel_root/src/services/titan_localization.c" \
    "$kernel_root/tests/test_app_runtime_platform_stubs.c" \
    "$kernel_root/tests/test_app_runtime_module.c" \
    -lm \
    "$kernel_root/tests/test_storage_scheduler_notify_stub.c" \
    -o /tmp/project_titan_app_runtime_module_test

ASAN_OPTIONS=detect_leaks=0 \
    /tmp/project_titan_app_runtime_module_test \
    "$kernel_root/tests/fixtures/hello_titan_mips32.tmod" \
    "$kernel_root/tests/fixtures/hello_titan_r5900_contract.tmod" \
    "$kernel_root/tests/fixtures/hello_titan_r5900_bad_cop1.tmod"

gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix28_session_service.c" \
    "$kernel_root/src/services/titan_session_service.c" \
    "$kernel_root/src/services/titan_igr_compatibility.c" \
    -o /tmp/project_titan_bf28_session_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf28_session_test

gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix28_overlay_core.c" \
    "$kernel_root/src/services/titan_session_service.c" \
    "$kernel_root/src/services/titan_igr_compatibility.c" \
    -o /tmp/project_titan_bf28_overlay_core_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf28_overlay_core_test


gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix28_unified_input.c" \
    "$kernel_root/src/services/titan_unified_input.c" \
    -o /tmp/project_titan_bf28_unified_input_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf28_unified_input_test

gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix28_modern_gamepad_protocol.c" \
    "$kernel_root/src/services/titan_modern_gamepad_protocol.c" \
    -o /tmp/project_titan_bf28_modern_gamepad_protocol_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf28_modern_gamepad_protocol_test

gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/tests/bf28_iop_stubs" \
    -I"$PROJECT_ROOT/modules/titan-ul-engine/include" \
    "$PROJECT_ROOT/modules/titan-ul-engine/modules/pademu/ds34common.c" \
    "$kernel_root/tests/test_buildfix28_dualsense_pademu.c" \
    -o /tmp/project_titan_bf28_dualsense_pademu_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf28_dualsense_pademu_test
python3 "$kernel_root/tests/test_buildfix28_modern_input_contract.py" "$PROJECT_ROOT"

gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$PROJECT_ROOT/modules/titan-ul-engine/modules/pademu" \
    -I"$PROJECT_ROOT/modules/titan-ul-engine/include" \
    "$kernel_root/tests/test_buildfix28_xbox_usb_protocol.c" \
    "$PROJECT_ROOT/modules/titan-ul-engine/modules/pademu/xbox_protocol.c" \
    -o /tmp/project_titan_bf28_xbox_usb_protocol_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_bf28_xbox_usb_protocol_test
python3 "$kernel_root/tests/test_buildfix28_xbox_usb_transport_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix28_xbox_gip_init_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix28_xbox_unified_input_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix28_xbox_bt_transport_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix28_xbox_bt_report_contract.py" "$PROJECT_ROOT"
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$kernel_root/tests/test_buildfix28_xbox_ble_transport.c" \
    "$PROJECT_ROOT/modules/titan-ul-engine/modules/pademu/titan_ble_hogp.c" \
    "$PROJECT_ROOT/modules/titan-ul-engine/modules/pademu/titan_ble_crypto.c" \
    -o /tmp/project_titan_buildfix28_xbox_ble
ASAN_OPTIONS=detect_leaks=1 /tmp/project_titan_buildfix28_xbox_ble
python3 "$kernel_root/tests/test_buildfix28_xbox_early_input_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix28_keyboard_mouse_contract.py" "$PROJECT_ROOT"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DTITAN_ENGINE_OVERLAY_HOST -I"$PROJECT_ROOT/modules/titan-engine-overlay" \
    "$PROJECT_ROOT/modules/titan-engine-overlay/titan_engine_overlay.c" \
    "$kernel_root/tests/test_buildfix28_engine_overlay.c" \
    -o /tmp/project_titan_buildfix28_engine_overlay
ASAN_OPTIONS=detect_leaks=1 /tmp/project_titan_buildfix28_engine_overlay
python3 "$kernel_root/tests/test_buildfix28_ul_overlay_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix28_ul_overlay_resume_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix28_ul_return_contract.py" "$PROJECT_ROOT"

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
    -I"$kernel_root/src/platform/ps2" \
    -I"$kernel_root/src/ui" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_shell_busy.c" \
    "$kernel_root/src/services/titan_elf_validate.c" \
    "$kernel_root/src/services/titan_sas.c" \
    "$kernel_root/src/services/titan_applications.c" \
    "$kernel_root/tests/test_app_platform_stubs.c" \
    "$kernel_root/src/services/titan_session_service.c" \
    "$kernel_root/src/services/titan_time.c" \
    "$kernel_root/src/services/titan_game_gsm.c" \
    "$kernel_root/src/services/titan_neutrino.c" \
    "$kernel_root/src/services/titan_library_cache.c" \
    "$kernel_root/src/services/titan_library_scan.c" \
    "$kernel_root/tests/test_library_scan.c" \
    "$kernel_root/tests/test_localization_compat_stub.c" \
    -o /tmp/project_titan_0_3_90_library_scan_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_library_scan_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_home_folders_test/TITAN"' \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_home_folders.c" \
    "$kernel_root/tests/test_home_folders.c" \
    -o /tmp/project_titan_0_3_90_home_folders_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_home_folders_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_shell_layout_test/TITAN"' \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_shell_layout.c" \
    "$kernel_root/tests/test_shell_layout.c" \
    -o /tmp/project_titan_0_3_90_shell_layout_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_shell_layout_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_dex_test/TITAN"' \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_dex.c" \
    "$kernel_root/tests/test_dex.c" \
    -o /tmp/project_titan_0_3_90_dex_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_0_3_90_dex_test

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_video_persistence_test/TITAN"' \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_video_output.c" \
    "$kernel_root/tests/test_video_output_persistence.c" \
    -o /tmp/project_titan_video_persistence_test_runner

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_video_persistence_test_runner

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_STORAGE_IO_TESTING \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_theme_test/TITAN"' \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_theme.c" \
    "$kernel_root/tests/test_theme_service.c" \
    -o /tmp/project_titan_theme_test_runner

rm -rf /tmp/project_titan_theme_test
mkdir -p /tmp/project_titan_theme_test
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_theme_test_runner

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/services" \
    -fsyntax-only \
    "$kernel_root/src/services/titan_sas.c" \
    "$kernel_root/src/services/titan_applications.c" \
    "$kernel_root/tests/test_app_platform_stubs.c" \
    "$kernel_root/src/services/titan_theme.c" \
    "$kernel_root/src/services/titan_package_installer.c" \
    "$kernel_root/src/services/titan_video_output.c" \
    "$kernel_root/src/services/titan_dex.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    -fsyntax-only \
    "$kernel_root/src/services/titan_neutrino.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -DPROJECT_TITAN_WIDESCREEN=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    -I"$kernel_root/src/platform/ps2" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/ui" \
    -I"$PROJECT_ROOT/modules/titan-ul-engine/include" \
    -fsyntax-only \
    "$kernel_root/src/ui/titan_shell_scene.c" \
    "$kernel_root/src/ui/titan_notifications.c" \
    "$kernel_root/src/ui/titan_system_error.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -DPROJECT_TITAN_WIDESCREEN=1 \
    -DTITAN_VIDEO_RENDERER_GS=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/ui" \
    -fsyntax-only \
    "$kernel_root/src/ui/titan_ui_assets.c" \
    "$kernel_root/src/ui/titan_icons.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -D_EE=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    -I"$kernel_root/src/platform/ps2" \
    -I"$kernel_root/src/ui" \
    -fsyntax-only \
    "$kernel_root/src/services/titan_profile_storage.c" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_shell_busy.c" \
    "$kernel_root/src/services/titan_storage_scheduler.c" \
    "$kernel_root/src/services/titan_vmc.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -D_EE=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/services" \
    -fsyntax-only \
    "$kernel_root/src/services/titan_sas.c" \
    "$kernel_root/src/services/titan_applications.c" \
    "$kernel_root/tests/test_app_platform_stubs.c" \
    "$kernel_root/src/services/titan_neutrino.c" \
    "$kernel_root/src/services/titan_exec_handoff.c" \
    "$kernel_root/src/services/titan_video_output.c" \
    "$kernel_root/src/services/titan_dex.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    -I"$kernel_root/src/platform/ps2" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/ui" \
    -fsyntax-only \
    "$kernel_root/src/services/titan_library_scan.c" \
    "$kernel_root/src/graphics/titan_wave_vu1.c" \
    "$kernel_root/src/ui/titan_xmb_spline.c" \
    "$kernel_root/src/ui/titan_shell_background.c" \
    "$kernel_root/src/core/titan_kernel.c"


gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -DPROJECT_TITAN_WIDESCREEN=1 \
    -DTITAN_VIDEO_RENDERER_GS=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -fsyntax-only \
    "$kernel_root/src/graphics/titan_wave_vu1.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -DTITAN_VIDEO_RENDERER_GS=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -fsyntax-only \
    "$kernel_root/src/graphics/titan_video_mode.c" \
    "$kernel_root/src/graphics/titan_video_memory.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -DPROJECT_TITAN_WIDESCREEN=1 \
    -DTITAN_VIDEO_RENDERER_GS=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/graphics" \
    -fsyntax-only \
    "$kernel_root/src/graphics/titan_video.c"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -DPROJECT_TITAN_WIDESCREEN=1 \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    -I"$kernel_root/src/platform/ps2" \
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/ui" \
    -fsyntax-only \
    "$kernel_root/src/services/titan_app_runtime.c" \
    "$kernel_root/src/services/titan_app_assets.c"

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
    -I"$kernel_root/src/graphics" \
    -I"$kernel_root/src/services" \
    -I"$kernel_root/src/platform/ps2" \
    -I"$kernel_root/src/ui" \
    "$kernel_root/src/ui/titan_xmb_spline.c" \
    "$kernel_root/tests/test_xmb_spline.c" \
    -lm \
    -o /tmp/project_titan_xmb_spline_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_xmb_spline_test

python3 "$kernel_root/tests/test_release_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_renderer_rewrite_contract.py"
python3 "$kernel_root/tests/test_titan_app_window_media_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_system_browser_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix27_network_client_browser_contract.py" "$(cd "$kernel_root/.." && pwd)"
python3 "$kernel_root/tests/test_buildfix27_app_network_api_contract.py" "$(cd "$kernel_root/.." && pwd)"
python3 "$kernel_root/tests/test_theme_engine_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_vu1_wave_no_internal_edges_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_xmb_spline_contract.py"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -DTITAN_AUDIO_TESTING \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_audio_test/TITAN"' \
    -I"$kernel_root/tests/host_stubs" \
    -I"$kernel_root/include" \
    -I"$kernel_root/src/core" \
    -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_theme.c" \
    "$kernel_root/src/services/titan_theme_lifecycle.c" \
    "$kernel_root/src/services/titan_audio.c" \
    "$kernel_root/tests/test_audio_runtime.c" \
    -o /tmp/project_titan_audio_runtime_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_audio_runtime_test
python3 "$kernel_root/tests/test_magenta_audio_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_home_grid_ul_contract.py" "$kernel_root"

gcc \
    -std=gnu99 \
    -Wall \
    -Wextra \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_utf8.c" \
    "$kernel_root/src/services/titan_utf8.c" \
    -o /tmp/project_titan_utf8_test

ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_utf8_test

python3 "$kernel_root/tests/test_source_hardening_contract.py" "$(cd "$kernel_root/.." && pwd)"

python3 "$kernel_root/tests/test_orbis_library_global_motion_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix20_update_package_contract.py"

rm -rf /tmp/project_titan_system_update_test
mkdir -p /tmp/project_titan_system_update_test/TITAN/update
mkdir -p /tmp/project_titan_system_update_test/TITAN/packages
printf 'old-kernel' > /tmp/project_titan_system_update_test/kernel.elf
printf 'new-kernel-build37' > /tmp/project_titan_system_update_test/new-kernel.elf
printf 'new-settings-build37' > /tmp/project_titan_system_update_test/new-settings.cfg
printf 'bad-kernel-build38' > /tmp/project_titan_system_update_test/bad-kernel27.elf
printf 'boot-file-build38' > /tmp/project_titan_system_update_test/boot-file27.bin
printf 'block bootloader directory' > /tmp/project_titan_system_update_test/bootloader
python3 "$kernel_root/tools/titan_pup.py" build \
    --output /tmp/project_titan_system_update_test/TITAN/update/update.pup \
    --build 39 --version 0.3.90-build39 \
    --file kernel.elf=/tmp/project_titan_system_update_test/new-kernel.elf \
    --file TITAN/settings/update-test.cfg=/tmp/project_titan_system_update_test/new-settings.cfg >/dev/null
python3 "$kernel_root/tools/titan_pup.py" build \
    --output /tmp/project_titan_system_update_test/TITAN/update/rollback.pup \
    --build 40 --version 0.3.90-build40 \
    --file kernel.elf=/tmp/project_titan_system_update_test/bad-kernel27.elf \
    --file bootloader/fail.bin=/tmp/project_titan_system_update_test/boot-file27.bin >/dev/null
printf 'corrupt-kernel-build39' > /tmp/project_titan_system_update_test/corrupt-kernel28.elf
python3 "$kernel_root/tools/titan_pup.py" build \
    --output /tmp/project_titan_system_update_test/TITAN/update/corrupt.pup \
    --build 41 --version 0.3.90-build41 \
    --file kernel.elf=/tmp/project_titan_system_update_test/corrupt-kernel28.elf >/dev/null
python3 - <<'PY_CORRUPT_PUP'
from pathlib import Path
p=Path('/tmp/project_titan_system_update_test/TITAN/update/corrupt.pup')
data=bytearray(p.read_bytes())
# Header 64 + one table entry 144 = payload starts at 208.
data[208] ^= 0x5A
p.write_bytes(data)
PY_CORRUPT_PUP

gcc \
    -std=gnu99 -Wall -Wextra -Werror \
    -DTITAN_STORAGE_IO_TESTING=1 \
    -DTITAN_STORAGE_DEVICE_ROOT='"/tmp/project_titan_system_update_test/TITAN"' \
    -DTITAN_SYSTEM_UPDATE_DEVICE_ROOT='"/tmp/project_titan_system_update_test"' \
    -I"$kernel_root/include" -I"$kernel_root/src/services" \
    "$kernel_root/src/services/titan_storage_io.c" \
    "$kernel_root/src/services/titan_system_update.c" \
    "$kernel_root/tests/test_system_update.c" \
    -o /tmp/project_titan_system_update_test_bin
/tmp/project_titan_system_update_test_bin

python3 "$kernel_root/tests/test_wlaunchelf_native_port_contract.py" "$kernel_root/.."
python3 "$kernel_root/tests/test_buildfix21_ui_wlaunchelf_merge_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix24_flow_errors_wlaunchelf_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix25_stream_updater_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix27_flow_ps4_wlaunchelf_pup_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_buildfix28_reference_flow_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix29_organic_flow_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix30_smooth_flow_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix31_vu1_procedural_flow_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix32_stochastic_flow_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_buildfix34_snake_flow_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_build25_flow_themes_power_autoreboot_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_build35_dynamic_theme_api_v2_contract.py" "$kernel_root"

python3 "$kernel_root/tests/test_build36_complex_theme_api_v3_contract.py" "$kernel_root"
python3 "$kernel_root/tests/test_installer_poc_v1.py"

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
    "$kernel_root/tests/test_localization_compat_stub.c" \
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
python3 "$kernel_root/tests/test_storage_artwork_fairness_contract.py" "$kernel_root"

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

# BuildFix29 CP00 — Unified Input final platform core.
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix29_input_platform.c" \
    "$kernel_root/src/services/titan_unified_input.c" \
    -o /tmp/project_titan_buildfix29_input_platform
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix29_input_platform
python3 "$kernel_root/tests/test_buildfix29_unified_input_final_contract.py" "$kernel_root"

# BuildFix29 CP01 — PADEMU modern identity/system-button/battery telemetry bridge.
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DTITAN_PADEMU_BRIDGE_HOST_TEST \
    -I"$kernel_root/src/services" -I"$PROJECT_ROOT/modules/titan-ul-engine/include" \
    "$kernel_root/tests/test_buildfix29_pademu_bridge.c" \
    "$kernel_root/src/services/titan_pademu_input_bridge.c" \
    "$kernel_root/src/services/titan_unified_input.c" \
    -o /tmp/project_titan_buildfix29_pademu_bridge
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix29_pademu_bridge
python3 "$kernel_root/tests/test_buildfix29_pademu_telemetry_contract.py" "$PROJECT_ROOT"

# BuildFix29 CP02 — System Button routing across Native/UL/Neutrino.
python3 "$kernel_root/tests/test_buildfix29_system_button_contract.py" "$PROJECT_ROOT"

# BuildFix29 CP03 — controller assignment/notification + adaptive Welcome.
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DTITAN_CONTROLLER_UX_HOST_TEST \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix29_controller_ux.c" \
    "$kernel_root/src/services/titan_controller_ux.c" \
    "$kernel_root/src/services/titan_unified_input.c" \
    -o /tmp/project_titan_buildfix29_controller_ux
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix29_controller_ux
python3 "$kernel_root/tests/test_buildfix29_controller_notification_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix29_welcome_input_contract.py" "$PROJECT_ROOT"

# BuildFix29 CP04 — honest controller battery telemetry.
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kernel_root/src/services" \
    "$kernel_root/tests/test_buildfix29_controller_battery.c" \
    "$kernel_root/src/services/titan_unified_input.c" \
    -o /tmp/project_titan_buildfix29_controller_battery
ASAN_OPTIONS=detect_leaks=0 /tmp/project_titan_buildfix29_controller_battery
python3 "$kernel_root/tests/test_buildfix29_controller_battery_contract.py" "$PROJECT_ROOT"

# BuildFix29 CP05 — Input DEX diagnostics and low-overhead telemetry.
python3 "$kernel_root/tests/test_buildfix29_input_dex_contract.py" "$PROJECT_ROOT"
python3 "$kernel_root/tests/test_buildfix29_input_overhead_contract.py" "$PROJECT_ROOT"

# BuildFix29 CP06 — public SDK/API pre-freeze.
python3 "$kernel_root/tests/test_buildfix29_api_prefreeze_contract.py" "$PROJECT_ROOT"

# BuildFix29 CP07 — open-source readiness and contributor release-safety gate.
# Runtime/hardware builds contain intentional target payloads (ELF/IRX) and must
# not be rejected by the public source-release cleanliness gate.  The gate is
# skipped only when the caller explicitly selects hardware-build context.
if [[ "${TITAN_SKIP_SOURCE_RELEASE_AUDIT:-0}" == "1" ]]; then
    echo "titan-open-source-readiness-contract: SKIP hardware-build-context"
else
    python3 "$kernel_root/tests/test_buildfix29_open_source_readiness_contract.py" "$PROJECT_ROOT"
fi

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

# RC1 hardware closure: canonical Titan Native ABI.
python3 "$kernel_root/tests/test_rc1_titan_native_n32_unification_contract.py" "$PROJECT_ROOT"

# RC1 real-hardware Zero-Stall closure: no frame FileXio HID, async BT pairing,
# and hardware-readable Early Input / pairing state diagnostics.
python3 "$kernel_root/tests/test_rc1_zero_stall_input_bt_contract.py" "$PROJECT_ROOT"

# B30 P3 — physical PADMAN + transport-only modern controllers coexist.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p3_dual_source_input_contract.py" "$PROJECT_ROOT"

python3 kernel/tests/test_b30_p4_bt_transport_diagnostics_contract.py .

# B30 P5 — Sony Classic BT pairing readiness is not gated by Xbox BLE scan.
python3 kernel/tests/test_b30_p5_pairing_semantics_contract.py .

# B30 P6: embedded Early Input PADEMU must match current source revision.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p6_pademu_embed_freshness_contract.py" "$PROJECT_ROOT"

# B30 P8 — bounded IOP Bluetooth flight recorder, asynchronously drained into Titan Diagnostics.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p8_bt_flight_recorder_contract.py" "$PROJECT_ROOT"

# B30 P9.1 — hardware-gated late PADEMU transport init.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p91_pair_on_demand_contract.py" "$PROJECT_ROOT"

# B30 P9.2 — Sony USB host provisioning + serialized HCI startup.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p92_sony_usb_pairing_contract.py" "$PROJECT_ROOT"

# B30 P10 — Settings-native Accessories/Storage, direct Classic BT discovery/SSP/bond,
# PADEMU routing, and bounded background storage inventory.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p10_wireless_accessories_storage_contract.py" "$PROJECT_ROOT"
cc -std=c11 -Wall -Wextra -Werror -I"$PROJECT_ROOT/modules/titan-ul-engine/include" \
  "$PROJECT_ROOT/kernel/tests/test_b30_p10_wireless_wire_abi.c" -o /tmp/titan_b30_p10_wireless_wire_abi
/tmp/titan_b30_p10_wireless_wire_abi
rm -f /tmp/titan_b30_p10_wireless_wire_abi

# B30 P10.1/P10.2 — physical accessory truth + nonblocking BT transport startup.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p101_accessory_truth_scan_retry_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p102_async_bt_init_rpc_contract.py" "$PROJECT_ROOT"

python3 kernel/tests/test_b30_p105_bt_bond_storage_ownership_contract.py

# B30 P10.5.2 — BondSync SIF RX capacity + storage worker liveness.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p1052_bt_rpc_rx_contract.py" "$PROJECT_ROOT"

# B30 P10.5.3 — pairing-time SIF QoS and BT flight-recorder retention.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p1053_bt_pairing_rpc_qos_contract.py" "$PROJECT_ROOT"

# B30 P10.6 — persistent controller session / assignment / Multitap.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p106_persistent_controller_session_contract.py" "$PROJECT_ROOT"

# B30 P10.6.1 — Health startup bootstraps BT transport without discovery.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p1061_startup_bt_bootstrap_contract.py" "$PROJECT_ROOT"

# B30 P10.6.2 — late explicit Classic scan after Health BT bootstrap.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p1062_late_classic_scan_contract.py" "$PROJECT_ROOT"

# B30 P10.6.3 — deferred P10.5 persisted-bond restore after late BT RPC bind.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p1063_deferred_bond_restore_contract.py" "$PROJECT_ROOT"

# B30 P10.6.4 — BT Bulk-IN runtime rearm reliability.
python3 "$PROJECT_ROOT/kernel/tests/test_b30_p1064_bt_rx_rearm_reliability_contract.py" "$PROJECT_ROOT"


# B30 P162 — TitanEYE native VU0/VU1 geometry, PATH1/PATH3 parity and lifecycle safety.
python3 "$PROJECT_ROOT/kernel/tests/test_p162_native_geometry_contract.py" "$PROJECT_ROOT"
cc -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$PROJECT_ROOT/ports/goldeneye/mgb64/src/platform/fast3d" \
  "$PROJECT_ROOT/kernel/tests/test_p162_vu0_host.c" \
  "$PROJECT_ROOT/ports/goldeneye/mgb64/src/platform/fast3d/gfx_ps2_vu0.c" \
  -o /tmp/titan_p162_vu0_host
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/titan_p162_vu0_host
rm -f /tmp/titan_p162_vu0_host

# B30 P162.3/P162.4 — gunbarrel provenance, historical cull, 440x299 I8 lane and chunk heap.
python3 "$PROJECT_ROOT/kernel/tests/test_p1623_gunbarrel_provenance_contract.py" "$PROJECT_ROOT"
python3 "$PROJECT_ROOT/kernel/tests/test_p1624_eye_heap_contract.py" "$PROJECT_ROOT/kernel"
cc -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$PROJECT_ROOT/kernel/include" \
  -I"$PROJECT_ROOT/ports/goldeneye" \
  "$PROJECT_ROOT/kernel/tests/test_p1624_i8_bridge.c" \
  "$PROJECT_ROOT/ports/goldeneye/titan_ge_platform.c" \
  -o /tmp/titan_p1624_i8_bridge
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/titan_p1624_i8_bridge
rm -f /tmp/titan_p1624_i8_bridge


# B30 P162 final hardware-state contract — preserve the geometry/index fixes
# that survived into the validated candidate while explicitly rejecting the
# superseded P162.5 experimental heap/fast-combiner branches.
python3 "$PROJECT_ROOT/kernel/tests/test_p1625_emergency_contract.py" "$PROJECT_ROOT"
