#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1])
h = (root/'src/services/titan_unified_input.h').read_text()
c = (root/'src/services/titan_unified_input.c').read_text()
need = [
'TITAN_INPUT_CAP_SYSTEM_BUTTON','TITAN_INPUT_BATTERY_UNSUPPORTED','TITAN_INPUT_BATTERY_UNKNOWN',
'TITAN_INPUT_BATTERY_LEVEL','TITAN_INPUT_BATTERY_CHARGING','TITAN_INPUT_BATTERY_FULL',
'TitanUnifiedInput_ClaimPlayerOnFirstInput','TitanUnifiedInput_UpdateBattery',
'TitanUnifiedInput_PollEvent','TitanUnifiedInput_GetWelcomeCandidate','preferred_player','stable_key'
]
for x in need:
    assert x in h or x in c, x
for bad in ('malloc(', 'calloc(', 'realloc(', 'free('):
    assert bad not in c, f'per-frame/static core heap use: {bad}'
assert 'g_boot_ui_active' in c
assert 'TITAN_INPUT_EVENT_SYSTEM_BUTTON' in c
assert 'TITAN_INPUT_BATTERY_UNSUPPORTED' in c
print('buildfix29-unified-input-final-contract: OK')
