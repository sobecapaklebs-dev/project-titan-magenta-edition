#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
ui=(root/'kernel/src/ui/titan_shell_scene.c').read_text()
assert 'DS34BT_STATE_USB_CONFIGURED is asserted only after' in bt
assert 'Do NOT submit WRITE_SCAN_ENABLE again from the pairing RPC' in bt
assert '(void)TitanBleHogp_StartScan(&xbox_ble);' in bt
assert 'if (!TitanBleHogp_StartScan(&xbox_ble)) ok = 0' not in bt
start=bt.index('int ds34bt_begin_pairing(void)')
end=bt.index('\n}', start)+2
body=bt[start:end]
assert 'hci_write_scan_enable(' not in body
assert 'return 1;' in body
assert 'pairing->state==TITAN_PADEMU_PAIRING_UNAVAILABLE || pairing->state==TITAN_PADEMU_PAIRING_ERROR' in ui
print('b30-p5-pairing-semantics: PASS Sony Classic readiness independent from Xbox BLE scan')
