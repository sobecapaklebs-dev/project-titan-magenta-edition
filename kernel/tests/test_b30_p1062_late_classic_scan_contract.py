#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
tele=(root/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
wire=(root/'kernel/src/services/titan_wireless_accessories.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()

def need(v,m):
    if not v:
        raise AssertionError(m)

need('TITAN_PADEMU_WIRELESS_BOOTSTRAP=5' in tele,'P10.6.1 BOOTSTRAP missing')
need('#define TITAN_PADEMU_RPC_RX_BYTES 384u' in rpc,'P10.5.2 RX protection missing')
need('g_bond_storage_write_pending' in bridge and 'g_bond_ack_requested' in bridge,
     'P10.5 storage ownership missing')
need('P10.5.7' in bt or 'TITAN_WIRELESS_BA(22)' in bt,'P10.5.7 ACL reassembly missing')
need('iomanX_open(' not in bt and 'iomanX_write(' not in bt,
     'filesystem I/O returned to ds34bt IOP')

fn=bt.index('int ds34bt_wireless_command')
begin=bt.index('if (command == TITAN_PADEMU_WIRELESS_BEGIN_SCAN)',fn)
end=bt.index('} else if (command ==',begin)
scan=bt[begin:end]

for token in (
    'g_wireless_scan_requested = 1;',
    'g_wireless_bootstrap_scan_pending = 0;',
    'bt_dev.controlEndp >= 0',
    'bt_dev.status & DS34BT_STATE_USB_CONFIGURED',
    'sony_host_bdaddr_valid',
    '!g_wireless_hci_startup_ready',
    'g_wireless_hci_startup_ready = 1;',
    '0x50313632u',
    'wireless_start_classic_scan_sequence()',
):
    need(token in scan,f'late BEGIN_SCAN repair token missing: {token}')

need(scan.index('0x50313632u') < scan.index('wireless_start_classic_scan_sequence()'),
     'late readiness is published after scan-start decision')
need(scan.index('g_wireless_bootstrap_scan_pending = 0;') <
     scan.index('0x50313632u'),
     'P10.4 pending-scan consume ordering changed')

ws=wire.index('void TitanWirelessAccessories_BeginStartupReconnect(void)')
we=wire.index('void TitanWirelessAccessories_EndStartupReconnect(void)',ws)
startup=wire[ws:we]
need('TITAN_PADEMU_WIRELESS_BOOTSTRAP' in startup,'startup no longer requests BOOTSTRAP')
need('TITAN_PADEMU_WIRELESS_BEGIN_SCAN' not in startup,'startup now performs discovery')
need('0x50313632u' not in startup,'late-scan repair leaked into startup service')

need('request.command != TITAN_PADEMU_WIRELESS_QUERY' in rpc,
     'P10.2 non-QUERY transport-init gate missing')
need('pademu_transport_request_init()' in rpc,'P10.2 async transport init missing')

print('b30-p1062-late-classic-scan: PASS explicit late BEGIN_SCAN recovers Classic readiness; Health BOOTSTRAP remains no-scan; P10.5/P10.5.7 preserved')
