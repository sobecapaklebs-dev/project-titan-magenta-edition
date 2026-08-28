#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
ds = (root / 'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()

def need(v, m):
    assert v, m

need('P10.5.7: HCI ACL -> complete L2CAP PDU reassembly before HID parsing.' in ds,
     'P10.5.7 state marker missing')
need('pb = (u8)((handle_flags >> 12) & 0x03u);' in ds,
     'HCI PB extraction missing')
need('if (pb == 0x01u)' in ds,
     'PB=01 continuation handling missing')
need('&l2cap_buf[4], copy_len' in ds,
     'continuation HCI header is not stripped')
need('expected = (u16)(8u + l2cap_payload_len);' in ds,
     'L2CAP complete size derivation missing')
need('g_p1057_acl_reassembly[2]' in ds and 'g_p1057_acl_reassembly[3]' in ds,
     'synthetic HCI ACL length repair missing')
need('goto p1057_rearm;' in ds and 'p1057_rearm:' in ds,
     'incomplete fragments can leak into old parser')
need('TITAN_WIRELESS_BA(22)' in ds and 'TITAN_WIRELESS_BA(23)' in ds,
     'bounded reassembly diagnostics missing')

cb = ds.rfind('static void l2cap_event_cb(int resultCode, int bytes, void *arg)\n{')
prep = ds.find('p1057_acl_prepare(resultCode, bytes, &p1057_parse_bytes)', cb)
parse = ds.find('L2CAP_event_task(resultCode, bytes)', cb)
need(cb >= 0 and prep > cb and parse > prep,
     'L2CAP parser is not gated by reassembly')

# Hardware evidence from 2026-08-24: full DualSense BT report is split into
# a 56-byte first HCI ACL packet and a 35-byte PB=01 continuation. The second
# packet's 4-byte HCI header is transport framing, not part of the L2CAP PDU.
first_hci_bytes = 56
continuation_hci_bytes = 35
reassembled = first_hci_bytes + continuation_hci_bytes - 4
need(reassembled == 87, 'hardware fragmentation model must reconstruct 87 bytes')
need(87 == 4 + 4 + 1 + 78,
     'DualSense full BT framing invariant changed')

# Preserve already-proven stages.
need('P10.5.6 Sony basic BT report 0x01' in ds, 'P10.5.6 basic parser regressed')
need('hid_request_sony_bt_feature05' in ds, 'P10.5.6 Feature05 regressed')
need('data[9] == 0x31' in ds and 'if (bytes < 87)' in ds,
     'DualSense 0x31 full parser regressed')
need('TITAN_WIRELESS_BA(19)' in ds, 'P10.5.5 classification regressed')
need('TITAN_WIRELESS_BA(13)' in ds, 'P10.5.4 handoff regressed')

print('b30-p1057-hci-acl-reassembly: PASS 56+35 HCI fragmentation -> 87-byte L2CAP/HID report; P10.5.6 preserved')
