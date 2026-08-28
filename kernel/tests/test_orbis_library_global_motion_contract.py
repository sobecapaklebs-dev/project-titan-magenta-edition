#!/usr/bin/env python3
from pathlib import Path
import sys

kernel = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
root = kernel.parent
scene = (kernel / 'src/ui/titan_shell_scene.c').read_text()
catalog = (kernel / 'src/ui/titan_shell_catalog.c').read_text()
catalog_h = (kernel / 'src/ui/titan_shell_catalog.h').read_text()
neutrino_h = (kernel / 'src/services/titan_neutrino.h').read_text()
folders_h = (kernel / 'src/services/titan_home_folders.h').read_text()

# The Home Applications hub is replaced by Orbis-style Library. Library uses
# its own Orbis glyph; the legacy Applications glyph is preserved inside the
# Library Applications category. APP_HOME remains the DEX application catalog.
assert 'TITAN_SHELL_PAGE_LIBRARY' in catalog_h
assert '"Library"' in catalog
assert 'TITAN_ICON_LIBRARY' in catalog
assert 'Preserve the Applications glyph that used to live on Home' in scene
assert 'TITAN_SHELL_PAGE_LIBRARY' in catalog
assert '"Applications",\n        "Launch software installed for Project Titan"' not in catalog

# Library categories are the useful PS2 subset of Orbis Library. No store/PSN
# Purchased category is allowed.
for value in ('"Search"', '"All"', '"Folders"', '"Games"', '"Applications"', '"Name: A-Z"'):
    assert value in scene
assert '"Purchased"' not in scene
assert 'TitanShellScene_BuildLibraryEntries' in scene
assert 'TitanNeutrino_GetGameCount()' in scene
assert 'TitanApplications_GetCount()' in scene
assert 'TitanHomeFolders_GetCount()' in scene
assert 'TITAN_LIBRARY_VISIBLE_ITEMS' in scene

# The Library is a complete catalog, not Home's 15-tile presentation subset.
assert '#define TITAN_NEUTRINO_GAME_CAPACITY 256u' in neutrino_h
assert '#define TITAN_HOME_FOLDER_ITEM_CAPACITY 128u' in folders_h
assert 'TITAN_SHELL_LAYOUT_CAPACITY' not in scene[scene.index('static unsigned int TitanShellScene_BuildLibraryEntries'):scene.index('static unsigned int TitanShellScene_GetLibraryEntryCount')]

# Folder content has row-by-row overflow and a real scrollbar rather than 24-item paging.
folder_start = scene.index('static void TitanShellScene_DrawFolderContentScreen')
folder_end = scene.index('static void TitanShellScene_DrawFolderScreenForMode', folder_start)
folder = scene[folder_start:folder_end]
assert 'max_first_row' in folder
assert 'const unsigned int columns = 6u;' in folder
assert 'const unsigned int visible_rows = 3u;' in folder
assert 'first_candidate = first_row * columns;' in folder
assert '613.0f' in folder and 'thumb_h' in folder
assert '/ 24u) * 24u' not in folder

# Hierarchical folder screens use the same parent-left / child-right choreography
# as Settings categories rather than a naked alpha cut.
folder_ui_start = scene.index('static void TitanShellScene_DrawFolderUi')
folder_ui_end = scene.index('static void TitanShellScene_DrawHome', folder_ui_start)
folder_ui = scene[folder_ui_start:folder_ui_end]
assert 'previous_offset_x' in folder_ui
assert 'current_offset_x' in folder_ui
assert 'TitanVideo_SetTransform' in folder_ui
assert 'TitanVideo_ResetTransform' in folder_ui
assert 'package_installer_transition' in scene
settings_start = scene.index('static void TitanShellScene_DrawSettingsPage')
settings_end = scene.index('static int TitanShellScene_ExportDebugReport', settings_start)
settings = scene[settings_start:settings_end]
assert 'leaf_offset' in settings and 'leaf_alpha' in settings
assert 'TitanShellScene_DrawPackageInstaller' in settings

print('orbis-library-global-motion-contract: OK')
