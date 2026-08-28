#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
bg=(r/'kernel/src/ui/titan_shell_background.c').read_text()
mk=(r/'kernel/Makefile').read_text()
tool=(r/'kernel/tools/titan_theme_asset.py').read_text()
assert 'TitanShellBackground_LoadV4' in bg
assert 'TitanShellBackground_DrawV4' in bg
assert 'TitanThemeV4_Update' in bg
assert 'TitanVideo_UploadTransientAtlasPsm16' in bg
assert 'TitanVideo_DrawTransientAtlasRegionPsm16' in bg
assert 'g_scene_v4_rotated[256u * 128u]' in bg
assert 'g_scene_v4_upload[256u * 64u]' in bg
assert 'src/services/titan_theme_v4_runtime.o' in mk
assert 'animation' in tool and 'top_x_milli' in tool and 'duration_ms' in tool
# The render function itself must not allocate or perform storage I/O.
start=bg.index('static void TitanShellBackground_DrawV4(float top_transition')
end=bg.index('\n}\n',start)+3
body=bg[start:end]
for forbidden in ('malloc(', 'free(', 'TitanStorageIo_', 'fopen(', 'fread('):
    assert forbidden not in body, forbidden
print('buildfix28-theme-v4-renderer-contract: OK')
