#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(root/'src/ui/titan_shell_scene.c').read_text()
def req(cond,msg):
    if not cond: raise AssertionError(msg)
step=float(re.search(r'#define TITAN_PACKAGE_INSTALLER_ROW_STEP ([0-9.]+)f',s).group(1))
top=float(re.search(r'#define TITAN_PACKAGE_INSTALLER_ROW_TOP ([0-9.]+)f',s).group(1))
visible=int(float(re.search(r'#define TITAN_PACKAGE_INSTALLER_VISIBLE_ROWS ([0-9.]+)',s).group(1)))
req(step >= 78.0, 'package row step must cover the 78px selected-row chrome')
req(visible == 4, 'current release layout must expose four collision-safe rows')
req(top + (visible-1)*step + 73.0 < 424.0, 'all visible package rows must remain above the footer safe area')
req('TitanShellScene_CopyEllipsizedText' in s, 'width-based ellipsis helper missing')
req('TITAN_PACKAGE_INSTALLER_TEXT_WIDTH' in s, 'package text bound missing')
req('package->filename' in s and 'bounded_description' in s, 'filename/metadata must be bounded')
req('TITAN_SETTINGS_ROW_TOP + ((float)visible * TITAN_SETTINGS_ROW_STEP)' not in s[s.index('static void TitanShellScene_DrawPackageInstaller'):s.index('static void TitanShellScene_DrawSystemUpdatePage')], 'Package Installer must not reuse overlapping Settings row cadence')
print('package-installer-ui-contract: OK spacing, safe canvas, ellipsis bounds')
