#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1]).resolve()
checks = {
  'modules/titan-ul-engine/include/ds34common.h': ['DUALSENSE_PID', 'DUALSENSE_EDGE_PID', 'translate_pad_dualsense'],
  'modules/titan-ul-engine/modules/pademu/ds34usb.c': ['DUALSENSE_PID', 'translate_pad_dualsense', 'type = DUALSENSE'],
  'modules/titan-ul-engine/modules/pademu/ds34bt.c': ['sony_generation_pending', 'data[9] == 0x31', 'translate_pad_dualsense', 'data[9] == PS4_11_REPORT_ID'],
  'kernel/src/services/titan_modern_gamepad_protocol.c': ['TitanGamepad_ParseDualSenseUsb', 'TitanGamepad_ParseDualSenseBluetooth', 'TitanGamepad_ParseXboxGipInput'],
  'kernel/src/services/titan_unified_input.c': ['TitanUnifiedInput_SubmitLegacyPad', 'TitanUnifiedInput_GetPlayer'],
}
for rel, needles in checks.items():
    p=root/rel
    if not p.is_file(): raise SystemExit(f'missing {rel}')
    text=p.read_text(errors='replace')
    for n in needles:
        if n not in text: raise SystemExit(f'{rel}: missing {n}')
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text(errors='replace')
if 'if (ds34pad[pad].sony_generation_pending)\n        return 0;' not in bt:
    raise SystemExit('DualSense/DS4 BT generation gate missing')
if 'ds34pad[pad_idx].sony_generation_pending = 0;' not in bt:
    raise SystemExit('BT generation not committed by first input report')
print('buildfix28-modern-input-contract: OK')
