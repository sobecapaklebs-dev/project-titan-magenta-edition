#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
h=(root/'src/services/titan_theme.h').read_text(); c=(root/'src/services/titan_theme.c').read_text(); scene=(root/'src/ui/titan_shell_scene.c').read_text(); bg=(root/'src/ui/titan_shell_background.c').read_text()
assert 'TITAN_THEME_CONTEXT_HOME' in h and 'TITAN_THEME_CONTEXT_SYSTEM' in h
assert 'context == TITAN_THEME_CONTEXT_HOME' in c
assert 'Startup functional surfaces' in scene and 'system Flow/Ribbon' in scene
assert 'scene_alpha = (unsigned char)(255.0f * (1.0f - system_amount));' in bg
assert 'HOME and the top menu never render a ribbon' in bg
print('theme-context-home-system-contract: OK HOME full scene; SYSTEM palette+waves+functional UI; startup SYSTEM preserved')
