#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
u=(r/'kernel/src/services/titan_unified_input.c').read_text(); h=(r/'kernel/src/services/titan_unified_input.h').read_text()
k=(r/'kernel/src/core/titan_kernel.c').read_text(); ux=(r/'kernel/src/services/titan_controller_ux.c').read_text()
early=(r/'kernel/src/services/titan_early_input.c').read_text(); welcome=(r/'kernel/src/ui/titan_shell_scene.c').read_text()
assert 'preferred_player' in h and 'stable_key' in h and 'TitanUnifiedInput_ClaimPlayerOnFirstInput' in h
assert 'TITAN_INPUT_EVENT_PLAYER_ASSIGNED' in h and 'TITAN_INPUT_EVENT_SYSTEM_BUTTON' in h
assert 'TITAN_INPUT_BATTERY_UNSUPPORTED' in h and 'TITAN_INPUT_BATTERY_UNKNOWN' in h
assert 'percent_valid' in u and 'battery_valid = 0u' in u
assert 'TitanEarlyInput_Pump(TitanTime_NowMilliseconds())' in k
assert '10000' in early or '10000u' in early or '10000ull' in early.lower()
assert 'TitanUnifiedInput_SetBootUiActive(1)' in welcome and 'TitanUnifiedInput_SetBootUiActive(0)' in welcome
assert 'TitanControllerUx_Update(!TitanShellScene_IsStartupActive())' in k
assert 'if(home_ready)emit' in ux
print('titan-controller-battery-contract: OK')
print('titan-controller-usb-battery-contract: OK')
print('titan-controller-battery-unknown-contract: OK')
print('titan-controller-assignment-contract: OK')
print('titan-early-input-continuity-contract: OK')
