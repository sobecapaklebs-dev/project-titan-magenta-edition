#!/usr/bin/env python3
from pathlib import Path
r=Path(__file__).resolve().parents[2]
h=(r/'kernel/src/services/titan_theme.h').read_text(); c=(r/'kernel/src/services/titan_theme.c').read_text(); pkg=(r/'kernel/tools/titan_pkg.py').read_text(); tool=(r/'kernel/tools/titan_theme_asset.py').read_text()
assert 'TITAN_THEME_SHELL_API_VERSION 4u' in h
assert 'TITAN_THEME_BACKGROUND_SCENE_V4 3u' in h
assert 'TitanThemeSceneV4Style' in h and 'scene_v4' in h
assert 'strcmp(value, "scene-v4")' in c and 'scene_v4_manifest' in c
assert 'scene-v4 themes require shell_api=4' in pkg
assert 'compile-v4' in tool and 'format=titan-scene-v4' in tool
for forbidden in ('.elf','.irx','.tmod','.so','.vsm'):
    assert forbidden in tool
print('buildfix28-theme-v4-schema-contract: OK')
