#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); s=(r/'kernel/src/ui/titan_shell_scene.c').read_text()
for token in ['TitanShellScene_WelcomeCandidate','TitanShellScene_WelcomeGlyph','TitanShellScene_WelcomeConfirm','TITAN_INPUT_DEVICE_DUALSHOCK2','TITAN_INPUT_DEVICE_DUALSENSE','TITAN_INPUT_DEVICE_XBOX_SERIES','TITAN_NBTN_SYSTEM','TITAN_NBTN_CROSS','TitanUnifiedInput_ClaimPlayerOnFirstInput','TitanUnifiedInput_SetBootUiActive(1)','TitanUnifiedInput_SetBootUiActive(0)','TitanVideo_DrawRoundedRectAlpha']:
 assert token in s,token
assert 'return "X"' in s and 'return "PS"' in s and 'return "HOME"' in s
print('titan-welcome-device-glyph-contract: OK')
print('titan-welcome-first-input-contract: OK')
print('titan-welcome-player1-contract: OK')
