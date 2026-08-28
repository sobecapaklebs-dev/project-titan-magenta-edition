#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
proto=(root/'kernel/src/services/titan_modern_gamepad_protocol.c').read_text()
uih=(root/'kernel/src/services/titan_unified_input.h').read_text()
usb=(root/'modules/titan-ul-engine/modules/pademu/xbox_protocol.c').read_text()
assert 'TitanGamepad_ParseXboxGipPacket' in proto
assert 'TitanGamepad_ParseXboxBluetoothHid' in proto
assert 'TITAN_INPUT_DEVICE_XBOX_ONE' in uih and 'TITAN_INPUT_DEVICE_XBOX_SERIES' in uih
assert 'struct ds2report' in usb and 'titan_xbox_gip_parse_ds2' in usb
print('buildfix28-xbox-unified-input-contract: OK')
