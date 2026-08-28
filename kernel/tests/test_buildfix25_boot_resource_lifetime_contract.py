from pathlib import Path
import sys,re
k=Path(sys.argv[1])
platform=(k/'src/platform/ps2/titan_platform.c').read_text()
kernel=(k/'src/core/titan_kernel.c').read_text()
apps=(k/'src/services/titan_applications.c').read_text()
appassets=(k/'src/services/titan_app_assets.c').read_text()
bg=(k/'src/ui/titan_shell_background.c').read_text()
scene=(k/'src/services/titan_theme_scene_graph.c').read_text()
# Font phase must bracket the actual atlas creation/upload, not a later unrelated block.
pos_phase=platform.index('TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_FONT)')
pos_init=platform.index('TitanFont_Initialize()')
pos_upload=platform.index('TitanFont_UploadAtlas()')
assert pos_phase < pos_init < pos_upload
assert 'TitanVramBootGuard_EnterPhase(TITAN_VRAM_PHASE_FONT);\n    TitanAvatarPicker_Initialize' not in kernel
# SAS/application discovery records paths/metadata only: no GS/video residency calls.
for forbidden in ('TitanVideo_', 'TitanPikaVr_', 'TitanVideoMemory_'):
    assert forbidden not in apps
# App icon work is deferred; initialization itself uploads nothing.
init=appassets[appassets.index('void TitanAppAssets_Initialize'):appassets.index('void TitanAppAssets_RequestRebuild')]
assert 'TitanVideo_' not in init
assert 'build_requested' not in init or 'memset' in init
# Theme scene refresh releases CPU scene assets before replacement.
refresh=bg[bg.index('void TitanShellBackground_RefreshThemeAssets'):]
assert 'TitanShellBackground_FreeSceneAssets();' in refresh[:500]
# Advanced timeline residency remains bounded CURRENT/NEXT/RECENT <= 3.
assert 'r->resident_count=1u' in scene and 'r->resident_count++' in scene
assert scene.count('r->resident_count++') == 2
print('titan-boot-resource-lifetime-contract: PASS')
