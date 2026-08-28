#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
iop=(root/'modules/titan-ul-engine/modules/pademu/xbox_protocol.c').read_text()
ee=(root/'kernel/src/services/titan_modern_gamepad_protocol.c').read_text()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
mk=(root/'modules/titan-ul-engine/modules/pademu/Makefile').read_text()
assert 'titan_xbox_bt_hid_parse_ds2' in iop
assert 'TitanGamepad_ParseXboxBluetoothHid' in ee
assert 'p[0] != 0x01' in ee
assert 'xbox_ble_input' in bt and 'titan_xbox_bt_hid_parse_ds2' in bt
assert 'IOP_OBJS += ds34bt.o xbox_protocol.o' in mk, 'BT PADEMU does not link xbox_protocol.o'
for token in ['DS2ButtonStart','DS2ButtonSelect','DS2ButtonCross','DS2ButtonCircle','DS2ButtonSquare','DS2ButtonTriangle','DS2ButtonL1','DS2ButtonR1']:
    assert token in iop, token
print('buildfix28-xbox-bt-report-contract: OK')
