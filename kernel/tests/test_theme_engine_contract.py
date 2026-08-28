#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
scene = (root / "src/ui/titan_shell_scene.c").read_text()
theme = (root / "src/services/titan_theme.c").read_text()
pkg = (root / "src/services/titan_package_installer.c").read_text()
builder = (root / "tools/titan_pkg.py").read_text()
spline = (root / "src/ui/titan_xmb_spline.c").read_text()

assert "TITAN_SETTINGS_THEMES" in scene
assert '"Temas"' in scene or "TITAN_STR_THEMES" in scene
assert "TitanShellScene_OpenSettingsCategoryAnimated();" in scene
assert "settings_depth_transition = 0.0f;" in scene
assert "including Themes and DEX-only Debug Settings" in scene
assert "TitanTheme_Apply" in scene
assert "TitanTheme_GetShellStyle" in scene
assert "TITAN_PACKAGE_APP_THEME" in pkg
assert '"@theme"' in pkg
assert "TitanPackageInstaller_IsThemeFilename" in pkg
assert "TitanTheme_ValidateDirectoryForId" in pkg
assert "APP_THEME = 3" in builder
assert 'THEME_ENTRYPOINT = "@theme"' in builder
assert "flow3d-v1" in theme
assert "TitanXmbSpline_BuildFlow3dRow" in spline
assert "Gerstner" in spline
assert "perspective" in spline.lower()
print("theme-engine-contract: OK")
