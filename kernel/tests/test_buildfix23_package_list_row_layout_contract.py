#!/usr/bin/env python3
from pathlib import Path
import re,sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(root/'src/ui/titan_shell_scene.c').read_text()
def num(name): return float(re.search(r'#define '+name+r' ([0-9.]+)',s).group(1))
visible=int(num('TITAN_PACKAGE_INSTALLER_VISIBLE_ROWS')); top=num('TITAN_PACKAGE_INSTALLER_ROW_TOP'); step=num('TITAN_PACKAGE_INSTALLER_ROW_STEP')
# Generic selected-row outer chrome spans y-5 through y+73 (78 px). Require no overlap.
assert step >= 78.0
assert visible == 4
assert top + (visible-1)*step + 73.0 < 424.0
assert 'TitanShellScene_CopyEllipsizedText' in s and 'TITAN_PACKAGE_INSTALLER_TEXT_WIDTH' in s
print('titan-package-list-row-layout-contract: OK 4 visible 78px chrome-safe rows, bounded filename/metadata, footer-safe')
