#!/usr/bin/env python3
from pathlib import Path
import sys

root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
bridge_h=(root/'kernel/src/services/titan_pademu_input_bridge.h').read_text()
wire=(root/'kernel/src/services/titan_wireless_accessories.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()

def need(v,m):
    if not v:
        raise AssertionError(m)

need('TITAN_PADEMU_BOND_SYNC_IMPORT' in bridge,'P10.5 BondSync IMPORT missing')
need('g_bond_storage_load_pending' in bridge,'P10.5 LOAD trigger missing')
need('g_bond_import_requested' in bridge,'P10.5 split-phase import handoff missing')
need('TitanPademuInputBridge_RequestBondRestore(void)' in bridge,'P10.6.3 restore API missing')
need('TitanPademuInputBridge_RequestBondRestore(void);' in bridge_h,'P10.6.3 restore API declaration missing')
need('g_bond_restore_requested && g_bt_rpc.server' in bridge,'restore not guarded by live BT RPC')
need('g_bond_storage_load_pending = 1;' in bridge,'persisted LOAD trigger not rearmed')
need('0x50313633u' in bridge,'P163 breadcrumb missing')

start=bridge.index('void TitanPademuInputBridge_BeginFrame(void)')
tail=bridge[start:]
endrel=tail.find('\nvoid ',1)
bf=tail if endrel<0 else tail[:endrel]

frame=bf.index('g_frame++')
pp=bf.index('#if defined(__EE__) || defined(_EE)')
p163=bf.index('0x50313633u')
need(frame < pp < p163,'P163 must be inside EE section after g_frame++')

host=bf[:pp]
for forbidden in (
    'g_bond_restore_requested','g_bt_rpc','g_bond_rpc_pending',
    'g_bond_storage_write_pending','g_bond_ack_requested',
    'g_bond_storage_load_pending','0x50313633u'
):
    need(forbidden not in host,'EE-only restore state leaked into host BeginFrame: '+forbidden)

need('g_bond_import_requested' in bf and 'TITAN_PADEMU_BOND_SYNC_IMPORT' in bf,
     'existing P10.5 LOAD->IMPORT path missing from BeginFrame')

ws=wire.index('void TitanWirelessAccessories_BeginStartupReconnect(void)')
we=wire.index('void TitanWirelessAccessories_EndStartupReconnect(void)',ws)
boot=wire[ws:we]
need('TITAN_PADEMU_WIRELESS_BOOTSTRAP' in boot,'Health BOOTSTRAP missing')
need('TitanPademuInputBridge_RequestBondRestore();' in boot,'Health restore request missing')
need('TITAN_PADEMU_WIRELESS_BEGIN_SCAN' not in boot,'Health regressed to scan/re-pair')
need('TitanStorageIo_' not in boot,'Health performs filesystem I/O')

need('#define TITAN_PADEMU_RPC_RX_BYTES 384u' in rpc,'P10.5.2 SIF RX guard missing')
need('0x50313632u' in bt,'P10.6.2 P162 missing')
need('P10.5.7' in bt or 'TITAN_WIRELESS_BA(22)' in bt,'P10.5.7 ACL reassembly missing')
need('iomanX_open(' not in bt and 'iomanX_write(' not in bt,'filesystem I/O returned to ds34bt')

print('b30-p1063-deferred-bond-restore: PASS P163 scoped to EE; persisted LOAD rearmed after BT RPC; native g_bond_import_requested -> BondSync IMPORT preserved')
print('P1063_LOAD_FLAG=g_bond_storage_load_pending')
print('P1063_LOAD_REARM_VALUE=1')
print('P1063_IMPORT_HANDOFF=g_bond_import_requested')
