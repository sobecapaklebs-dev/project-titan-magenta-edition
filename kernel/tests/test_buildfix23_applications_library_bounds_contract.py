#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(root/'src/ui/titan_shell_scene.c').read_text()
assert '#define TITAN_LIBRARY_VISIBLE_COLUMNS 3' in s and '#define TITAN_LIBRARY_VISIBLE_ROWS 4' in s
assert 'TitanShellScene_CopyEllipsizedText' in s
assert 'TitanUiLayout_SelectSafeAnchor' in s
assert 'g_scene.library_item_position' in s and 'g_scene.applications_row_position' in s
print('titan-applications-library-bounds-contract: OK bounded text/focus lanes + DEX safe-anchor integration preserved')
