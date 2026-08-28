#!/usr/bin/env python3
from pathlib import Path
import re, sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
s=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
assert 'TITAN_DUALSENSE_PAIR_REPORT_ID 0x0A' in s
assert '#define TITAN_HID_REPORT_TYPE_FEATURE 0x03u' in s
assert 'HID_USB_GET_REPORT_FEATURE' not in s
assert '(TITAN_HID_REPORT_TYPE_FEATURE << 8)' in s

assert 'TITAN_DUALSENSE_PAIR_REPORT_SIZE 27' in s
for phase in ('0x53500000u','0x53500001u','0x53500002u','0x53500003u','0x53500004u','0x53500005u'):
    assert phase in s
assert 'mips_memcpy(sony_pair_buf + 1, sony_host_bdaddr, 6);' in s
assert 'mips_memcpy(sony_pair_buf + 7, link_key, sizeof(link_key));' in s
assert 'UsbSetDeviceConfiguration(chrg_end, config->bConfigurationValue, chrg_config_set, NULL);' in s
assert 'mips_memcpy(sony_host_bdaddr, hci_buf + 6, 6);' in s
assert 'P9.2: serialized HCI startup' in s
assert re.search(r'xbox_ble_random_count\s*==\s*16.*?TitanBleHogp_StartEarlyReconnect', s, re.S)
m=re.search(r'int\s+ds34bt_begin_pairing\s*\(\s*void\s*\)\s*\{',s); assert m
depth=0; end=None
for i in range(s.find('{',m.start()),len(s)):
    if s[i]=='{': depth+=1
    elif s[i]=='}':
        depth-=1
        if depth==0: end=i+1; break
pair=s[m.start():end]
assert 'sony_pair_requested = 1;' in pair
assert 'hci_write_scan_enable(' not in pair
assert 'requested = pademu_transport_init_on_demand();' in rpc
assert 'ready = pademu_transport_init_ready();' in rpc
assert 'g_tx.pairing.started = ds34bt_begin_pairing() ? 1u : 0u;' in rpc
assert 'PAD_INIT(' not in rpc
print('b30-p92-sony-usb-pairing: PASS semantic HCI serialization + DualSense USB report 0x0A + async P10.2 Pair semantics')
