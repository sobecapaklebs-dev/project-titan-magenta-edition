#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); u=(r/'kernel/src/services/titan_unified_input.c').read_text(); b=(r/'kernel/src/services/titan_pademu_input_bridge.c').read_text(); usb=(r/'modules/titan-ul-engine/modules/pademu/ds34usb.c').read_text(); bt=(r/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
for token in ['TITAN_INPUT_BATTERY_UNSUPPORTED','TITAN_INPUT_BATTERY_UNKNOWN','TITAN_INPUT_BATTERY_LEVEL','TITAN_INPUT_BATTERY_CHARGING','TITAN_INPUT_BATTERY_FULL','TitanUnifiedInput_UpdateBatteryEx','percent_valid']:
 assert token in u,token
assert 'TitanUnifiedInput_SubmitStateWithBattery' in b and 'p->battery_valid' in b
for src in (usb,bt):
 for token in ['TITAN_PADEMU_BATTERY_CHARGING','TITAN_PADEMU_BATTERY_FULL','TITAN_PADEMU_BATTERY_UNKNOWN']:
  assert token in src,token
assert 'battery_state = TITAN_PADEMU_BATTERY_UNSUPPORTED' in usb
print('titan-controller-battery-source-contract: OK')
