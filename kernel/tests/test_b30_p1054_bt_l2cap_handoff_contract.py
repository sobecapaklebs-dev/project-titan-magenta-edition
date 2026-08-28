#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
ds=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
ui=(root/'kernel/src/services/titan_wireless_accessories.c').read_text()

def need(v,m):
    assert v,m

need('static int wireless_find_pad(const u8 address[6])' in ds,
     'existing Classic pad lookup missing')
need('HCI Create Connection against an ACL that already exists' in ds,
     'IOP CONNECT idempotence guard missing')
need('hci_buf[2] == 0x0B && g_wireless_direct_pad >= 0' in ds,
     'Connection Already Exists is still fatal')
need('TITAN_WIRELESS_BA(18)' in ds,
     'idempotent CONNECT trace marker missing')
need('TITAN_WIRELESS_BA(13)' in ds and 'rc = L2CAP_Command(handle, cmd_buf, 8);' in ds,
     'L2CAP Connection Request TX return code is not recorded')
need('g_wireless_l2cap_watch_pad' in ds and
     'g_wireless_l2cap_watch_queries >= 8u' in ds and
     'g_wireless_l2cap_retry_count == 0u' in ds,
     'bounded first-L2CAP watchdog missing')
need('g_wireless_l2cap_retry_count = 1u;' in ds,
     'L2CAP retry is not one-shot bounded')
need('resultCode == USB_RC_OK && bytes >= 4' in ds and
     'rx_handle == ds34pad[g_wireless_l2cap_watch_pad].hci_handle' in ds,
     'first L2CAP RX does not disarm retry watchdog')
need('accessory->flags & (TITAN_PADEMU_WIRELESS_CONNECTING | TITAN_PADEMU_WIRELESS_CONNECTED)' in ui,
     'Settings UX can still enqueue duplicate CONNECT RPCs')
# Do not regress the P10.5 ownership rule while touching the handshake.
need('iomanX_open' not in ds and 'iomanX_write' not in ds,
     'PADEMU regained direct filesystem I/O')
print('b30-p1054-bt-l2cap-handoff: PASS idempotent connect; L2CAP TX evidence; one-shot no-RX retry')
