#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
scene = (root / 'src/ui/titan_shell_scene.c').read_text()
sdk = (root / 'include/titan_app_sdk.h').read_text()
update = (root / 'src/services/titan_system_update.h').read_text()
app = (root.parent / 'apps/wlaunchelf/src/wlaunchelf_titan.c').read_text()
update_c = (root / 'src/services/titan_system_update.c').read_text()
runtime_c = (root / 'src/services/titan_app_runtime.c').read_text()


def require(cond, msg):
    if not cond:
        raise SystemExit(msg)

require('#define TITAN_SHELL_FOOTER_TOP 424.0f' in scene and
        '#define TITAN_SHELL_FOOTER_HEIGHT 24.0f' in scene and
        '#define TITAN_SHELL_FOOTER_TEXT_Y 430.0f' in scene,
        'compact footer geometry regressed')
require('TitanShellScene_IsFolderScreenMode(g_scene.folder_ui_mode)' in scene and
        'if (!folder_fullscreen_active)' in scene and
        'if (!folder_fullscreen_active && home_alpha > 4)' in scene,
        'folder fullscreen no longer owns the content plane')
require('former near-opaque black fullscreen' in scene,
        'folder wave-only backdrop contract missing')
require('Do not\n     * paint opaque/black option cards' in scene,
        'folder transparent option contract missing')
require('"Offline"' not in scene[scene.index('static void TitanShellScene_DrawLibraryPage'):scene.index('static void TitanShellScene_OpenLibrarySelectedEntry')],
        'Library network label still competes with clock')
require('selected.page == TITAN_SHELL_PAGE_LIBRARY' in scene and
        'selected.page != TITAN_SHELL_PAGE_LIBRARY' in scene,
        'Library title right-edge exception missing')
require('storage_list' in sdk and 'launch_elf' in sdk,
        'wLaunchELF SDK extension missing after merge')
require('titan_app_init' in app and 'storage_list' in app,
        'wLaunchELF native app missing after merge')
require('TITAN_SYSTEM_UPDATE_CURRENT_BUILD ' in update,
        'system update generation missing')
require('TitanSystemUpdate_VerifyPayloadStep' in update_c and
        'TitanSystemUpdate_OpenPayloadStream' in update_c and
        'TITAN_SYSTEM_UPDATE_PREPARING' in update_c and
        'TitanStorageIo_Seek64(' not in update_c,
        'hardware-safe forward-only updater contract missing')
require('runtime mais novo (atualize o sistema)' in runtime_c,
        'clear incompatible-runtime Titan App message missing')
print('buildfix25-hardware-updater-contract: OK')
