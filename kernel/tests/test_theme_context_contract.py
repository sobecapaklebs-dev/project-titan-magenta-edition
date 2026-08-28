#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1]
s=(root/'src/ui/titan_shell_scene.c').read_text()
b=(root/'src/ui/titan_shell_background.c').read_text()
def req(v,m):
    if not v: raise AssertionError(m)
functional_pages = (
    'TITAN_SHELL_PAGE_SETTINGS', 'TITAN_SHELL_PAGE_USER_PROFILE',
    'TITAN_SHELL_PAGE_SAVED_DATA', 'TITAN_SHELL_PAGE_LIBRARY',
    'TITAN_SHELL_PAGE_APPLICATIONS', 'TITAN_SHELL_PAGE_OVERVIEW',
    'TITAN_SHELL_PAGE_APP_HOME', 'TITAN_SHELL_PAGE_FTP_HOME',
    'TITAN_SHELL_PAGE_BROWSER', 'TITAN_SHELL_PAGE_STORAGE',
    'TITAN_SHELL_PAGE_INPUT', 'TITAN_SHELL_PAGE_NOTIFICATIONS',
    'TITAN_SHELL_PAGE_POWER', 'TITAN_SHELL_PAGE_ABOUT',
)
context_start=s.index('theme_system_view_transition = 0.0f')
context_block=s[context_start:s.index('theme_style = TitanTheme_GetShellStyle()', context_start)]
for page in functional_pages:
    req(page in context_block, f'{page} must explicitly enter SYSTEM theme context')
req('TITAN_SHELL_PAGE_HOME' not in context_block, 'HOME must not be classified as SYSTEM theme context')
req('g_scene.page == TITAN_SHELL_PAGE_OVERVIEW' in context_block and 'theme_system_view_transition = TitanAnimation_SmoothStep' in context_block, 'Overview must preserve SYSTEM theme transition')
req('g_scene.startup_phase == TITAN_STARTUP_PHASE_HOME_ENTER ? 0.0f : 1.0f' in s, 'startup Welcome/Profile must use SYSTEM theme context while HOME_ENTER may reveal HOME scene')
req('system_flow_enabled' in b and 'scene_alpha = (unsigned char)(255.0f * (1.0f - system_amount));' in b, 'scene-v2 SYSTEM path must suppress full HOME artwork')
print('theme-context-contract: OK HOME full, non-HOME/system startup reduced theme context')
