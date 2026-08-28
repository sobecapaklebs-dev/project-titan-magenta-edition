#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1])
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
hogp=(root/'modules/titan-ul-engine/modules/pademu/titan_ble_hogp.c').read_text()
crypto=(root/'modules/titan-ul-engine/modules/pademu/titan_ble_crypto.c').read_text()
mk=(root/'modules/titan-ul-engine/modules/pademu/Makefile').read_text()
for token in ['TitanBleHogp_StartScan','TitanBleHogp_StartEarlyReconnect','TITAN_BLE_CID_ATT','TITAN_BLE_CID_SMP','TITAN_BLE_UUID_HID','ATT_NOTIFY','SMP_PUBLIC_KEY','SMP_DHKEY_CHECK']:
    assert token in hogp, token
for token in ['TitanBleHogp_OnHciEvent','TitanBleHogp_OnAcl','xbox_ble_send_hci','xbox_ble_send_acl','ds34bt_set_xbox_ble_bond']:
    assert token in bt, token
assert 'titan_ble_hogp.o' in mk and 'titan_ble_crypto.o' in mk
assert 'titan_ble_sc_f4' in crypto and 'titan_ble_sc_f5' in crypto and 'titan_ble_sc_f6' in crypto
assert 'BTstack' not in mk and 'bluekitchen' not in bt.lower()
print('buildfix28-xbox-bt-transport-contract: OK')
