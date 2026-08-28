#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
b=(root/'src/ui/titan_shell_background.c').read_text()
h=(root/'src/ui/titan_xmb_spline.h').read_text()
c=(root/'src/ui/titan_xmb_spline.c').read_text()
def req(v,m):
    if not v: raise AssertionError(m)
req('TitanXmbSpline_DrawWithOpacity' in h, 'opacity-aware ribbon API missing')
req('vertical_transition, 1.0f' in c, 'legacy TitanXmbSpline_Draw must remain full-opacity wrapper')
scene_start=b.index('if (TitanTheme_GetActive()->background_engine == TITAN_THEME_BACKGROUND_SCENE_V2')
scene=b[scene_start:b.index('return;', scene_start)]
req('TitanXmbSpline_DrawWithOpacity' in scene and 'system_amount' in scene, 'SYSTEM ribbon must fade with system transition')
req('scene_alpha = (unsigned char)(255.0f * (1.0f - system_amount));' in b, 'HOME artwork must fade complementarily')
req('TitanShellBackground_Clamp01(horizontal_motion), system_amount' in scene, 'SYSTEM particles must fade with the same transition')
req('TitanXmbSpline_ModulateAlpha(style.alpha, opacity)' in c, 'VU1 ribbon layers must receive opacity')
req('fade * 0.78f * opacity' in c, 'CPU fallback ribbon must receive the same opacity')
print('theme-transition-crossfade-contract: OK complementary scene/ribbon alpha with VU1+fallback parity')
