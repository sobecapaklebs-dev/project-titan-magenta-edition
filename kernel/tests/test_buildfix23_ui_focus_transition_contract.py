#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
scene=(root/'src/ui/titan_shell_scene.c').read_text(); h=(root/'src/ui/titan_ui_layout_guard.h').read_text()
assert 'TitanUiFocus_Step(g_scene.selected_position' in scene
assert 'TitanUiFocus_Step(g_scene.top_menu_selected_position' in scene
assert 'TitanUiFocus_UniqueIndex' in h
print('titan-ui-focus-transition-contract: OK shared focus interpolation and unique target resolver')
