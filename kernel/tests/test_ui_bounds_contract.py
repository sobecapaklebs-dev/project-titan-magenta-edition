#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(root/'src/ui/titan_shell_scene.c').read_text()
def req(v,m):
    if not v: raise AssertionError(m)
row=s[s.index('static void TitanShellScene_DrawMenuOptionRow(', s.index('static void TitanShellScene_DrawNotificationsPage')):s.index('static void TitanShellScene_DrawSettingsListRow(', s.index('static void TitanShellScene_DrawMenuOptionRow(', s.index('static void TitanShellScene_DrawNotificationsPage')))]
req('bounded_title' in row and 'bounded_line1' in row and 'bounded_line2' in row, 'generic functional rows need bounded text')
req(row.count('TitanShellScene_CopyEllipsizedText(') >= 3, 'title/metadata/value must all be bounded')
fn=s[s.index('static unsigned int TitanShellScene_Utf8SequenceLength'):s.index('static void TitanShellScene_DrawPackageInstaller')]
req('0xc0u' in fn and '0x80u' in fn and 'complete UTF-8 code points' in fn, 'ellipsis must not split UTF-8 continuation bytes')
lib=s[s.index('static void TitanShellScene_DrawLibraryEntry'):s.index('static void TitanShellScene_DrawLibraryPage')]
req('TitanShellScene_CopyFittedTextPrefix' in lib and '82.0f' in lib and 'TitanShellScene_CopyEllipsizedText' in lib, 'Library title must use measured two-line bounds')
req('%.18s' not in lib, 'Library title must not split raw UTF-8 bytes at 18-byte boundaries')
dex=s[s.index('static void TitanShellScene_DrawDexOverlay'):s.index('static void TitanShellScene_DrawFooterText')]
layout=(root/'src/ui/titan_ui_layout_guard.h').read_text()
for anchor in ('TOP_LEFT','TOP_RIGHT','BOTTOM_LEFT','BOTTOM_RIGHT'):
    req('TITAN_UI_ANCHOR_'+anchor in layout, f'missing DEX safe anchor {anchor}')
req('TitanUiLayout_SelectSafeAnchor' in dex, 'DEX overlay must resolve through safe-anchor selector')
req('exclusions[0]' in dex and 'exclusions[1]' in dex and 'exclusions[2]' in dex, 'DEX overlay must exclude mandatory UI/footer regions')
print('ui-bounds-contract: OK bounded rows, UTF-8 ellipsis, DEX safe area')
