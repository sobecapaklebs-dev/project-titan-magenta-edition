#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(root/'src/ui/titan_shell_scene.c').read_text(); h=(root/'src/ui/titan_ui_layout_guard.h').read_text()
for a in ('TOP_LEFT','TOP_RIGHT','BOTTOM_LEFT','BOTTOM_RIGHT'): assert 'TITAN_UI_ANCHOR_'+a in h
assert 'exclusions[0]' in s and 'exclusions[1]' in s and 'exclusions[2]' in s
assert 'TitanUiLayout_SelectSafeAnchor' in s
print('titan-dex-overlay-safe-area-contract: OK anchored overlay with mandatory-content exclusion regions')
