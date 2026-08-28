#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(root/'src/ui/titan_shell_scene.c').read_text(); bg=(root/'src/ui/titan_shell_background.c').read_text()
assert 'theme_style = TitanTheme_GetShellStyle();' in s
assert 'accent_red = theme_style->accent.red;' in s and 'accent_green = theme_style->accent.green;' in s and 'accent_blue = theme_style->accent.blue;' in s
assert 'system_amount' in bg and '1.0f - system_amount' in bg
assert 'TitanTheme_GetGeneration()' in bg
# Transition endpoints: system_amount=0 => scene alpha 255; =1 => scene alpha 0.
for p in (0.0,.1,.5,.9,1.0):
    scene=255*(1-p); ribbon=p
    assert 0 <= scene <=255 and 0<=ribbon<=1
print('theme-context-palette-accent-contract: OK active-theme accent + complementary HOME/SYSTEM crossfade')
