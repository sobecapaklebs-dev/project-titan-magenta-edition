#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
project = root.parent

def req(cond, msg):
    if not cond:
        raise SystemExit(msg)

scene = (root/'src/ui/titan_shell_scene.c').read_text()
scheduler_h = (root/'src/services/titan_storage_scheduler.h').read_text()
scheduler_c = (root/'src/services/titan_storage_scheduler.c').read_text()
update_h = (root/'src/services/titan_system_update.h').read_text()
update_c = (root/'src/services/titan_system_update.c').read_text()
make = (root/'Makefile').read_text()

# All approved Flow variants share the hardware-proven head-driven Snake + VU1 contract.
expected = {
    'ribbon-scarlet': ('Ribbon Scarlet', '52391'),
    'ribbon-wine': ('Ribbon Vinho', '51391'),
    'ribbon-cyan': ('Ribbon Ciano', '62117'),
    'ribbon-twilight': ('Ribbon Crepusculo', '47831'),
}
for folder, (display_name, seed) in expected.items():
    cfg_path = project/'themes'/folder/'theme.cfg'
    req(cfg_path.exists(), f'missing Ribbon theme: {folder}')
    cfg = cfg_path.read_text()
    for token in (
        f'name={display_name}',
        'version=8.0.1-hwfix2',
        'background_engine=flow3d-v1',
        'primary_wave1_wander=0',
        'primary_wave1_snake=1',
        'primary_wave1_reference_ribbon=0',
        'primary_wave1_snake_speed_milli=2',
        'primary_wave1_snake_extent_x_milli=520000',
        'primary_wave1_snake_extent_y_milli=320000',
        'primary_wave1_snake_trail_frames_milli=4500',
        'primary_wave1_snake_visibility_min_milli=380',
        'vu1_deform_speed_milli=720',
        'vu1_deform_frequency_milli=1500',
        'vu1_deform_amplitude_milli=7200',
        'particle_alpha=0',
    ):
        req(token in cfg, f'{folder}: restored Flow token missing: {token}')
    req(f'primary_wave1_snake_seed={seed}' in cfg, f'{folder}: wrong Snake seed')

# Restore the approved Orbis-style gallery rather than the regressed list page.
for token in (
    '#define TITAN_THEME_GRID_COLUMNS 3',
    '#define TITAN_THEME_GRID_ROWS 2',
    'TitanShellScene_DrawThemeSelectionPage',
    'TitanShellScene_DrawThemePreview',
    'TitanShellScene_DrawThemeCheck',
    'X Aplicar    O Voltar    Direcional Selecionar',
    'g_scene.settings_selected_category == TITAN_SETTINGS_THEMES',
):
    req(token in scene, f'theme selector contract missing: {token}')

# Safe shutdown/restart: park storage first, ship PWROFF_IRX, close EE clients,
# and reset/sync the IOP before returning to ROM for a console restart.
req('int TitanStorageScheduler_IsPackageWorkerParked(void);' in scheduler_h,
    'storage parked acknowledgement is not public')
req('!g_storage_scheduler.initialized || !g_storage_scheduler.threaded' in scheduler_c,
    'non-threaded power quiesce fallback missing')
for token in (
    'TitanShellScene_QuiesceStorageForPower',
    'TitanShellScene_InitializePoweroffServer',
    'titan_poweroff_irx',
    'TitanBrowser_Shutdown();',
    'TitanAudio_Shutdown();',
    'TitanInput_Shutdown();',
    'TitanIopServiceManager_Shutdown();',
    'TitanIopServiceManager_Initialize(0, 0)',
    'LoadExecPS2("rom0:OSDSYS", 0, 0);',
    'poweroffShutdown();',
):
    req(token in scene, f'power transition contract missing: {token}')
req('$(PS2SDK)/iop/irx/poweroff.irx' in make and 'titan_poweroff_irx' in make,
    'poweroff IRX is not embedded by the build')

# Successful PUP updates must not release the FileXio lane back to background
# work; they cross directly into the same console restart path.
poll_start = scene.index('static void TitanShellScene_PollSystemUpdate(void)')
poll_end = scene.index('static int TitanShellScene_UpdateSystemUpdate', poll_start)
poll = scene[poll_start:poll_end]
req('status->result == TITAN_SYSTEM_UPDATE_OK' in poll and
    'TitanShellScene_RestartConsoleNow' in poll,
    'successful .pup does not auto-restart')
req('Atualizacao aplicada; reinicio automatico' in update_c,
    'updater completion status still asks for manual restart')

# Build 25 power/theme capabilities must survive the newer Build 35 generation.
req('TITAN_SYSTEM_UPDATE_CURRENT_BUILD 36u' in update_h,
    'Build 35 generation missing')
req('--build 36' in make and '--version 0.3.90-build36' in make,
    'Build 35 PUP metadata missing')
req('<= TITAN_SYSTEM_UPDATE_CURRENT_BUILD' in update_c,
    'forward-only updater policy was changed')
# The Flow themes remain in the source/runtime capability set. Their hardware-proven
# Snake/VU1 profile is validated from project/themes above.
req('build36-complex-theme-api-v3-compat7.pup' in make,
    'Build36 BuildFix34-compatible seven-entry bootstrap target missing')

print('build25-flow-themes-power-autoreboot-capability-contract: OK on Build36')
