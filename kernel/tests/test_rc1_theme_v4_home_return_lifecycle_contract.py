#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
scene = (root / 'src/ui/titan_shell_scene.c').read_text()
bg = (root / 'src/ui/titan_shell_background.c').read_text()

def req(cond, msg):
    if not cond:
        raise AssertionError(msg)

start = scene.index('static void TitanShellScene_UpdateThemeLifecycle(void)')
end = scene.index('void TitanShellScene_Render(', start)
life = scene[start:end]

req('g_scene.page != TITAN_SHELL_PAGE_HOME' in life, 'functional-page lifecycle branch missing')
req('g_scene.page_transition_target < 0.5f' in life, 'return-to-HOME direction is not detected')
req('TITAN_THEME_LIFECYCLE_RETURN_HOME' in life, 'return-to-HOME does not restore dynamic scene lifecycle')
req('1.0f - TitanShellScene_Clamp(g_scene.page_transition' in life, 'return scene opacity is not tied to HOME reveal')
req('TITAN_THEME_LIFECYCLE_HOME_EXIT' in life, 'HOME->functional-page crossfade lifecycle missing')
req('g_scene.page_transition < 0.999f' in life, 'stable submenu is not separated from transition')

v4 = bg[bg.index('if (TitanTheme_GetActive()->background_engine == TITAN_THEME_BACKGROUND_SCENE_V4'):]
req('TitanThemeLifecycle_GetSceneOpacity()' in v4, 'scene-v4 is not lifecycle opacity gated')
req('ribbon_opacity = 1.0f - scene_opacity;' in v4, 'scene-v4/Flow complementary crossfade missing')

print('rc1-theme-v4-home-return-lifecycle-contract: PASS persistent HOME scene + complementary Flow crossfade')
