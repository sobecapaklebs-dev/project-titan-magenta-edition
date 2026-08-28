#!/usr/bin/env python3
import re, sys
from pathlib import Path
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
def t(p):
    q=root/p; assert q.is_file(), f'missing {p}'; return q.read_text(errors='replace')
bt=t('modules/titan-ul-engine/modules/pademu/ds34bt.c')
rpc=t('modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c')
abi=t('modules/titan-ul-engine/include/titan_pademu_telemetry.h')
bridge=t('kernel/src/services/titan_pademu_input_bridge.c')
sched=t('kernel/src/services/titan_storage_scheduler.c')
diag=t('kernel/src/services/titan_diagnostics.c')
assert 'TITAN_PADEMU_RPC_BOND_SYNC 5' in abi
assert 'TitanPademuBondSync' in abi and 'TITAN_PADEMU_BOND_SYNC_ACK' in abi
assert 'ds34bt_bond_sync' in bt and 'TITAN_PADEMU_RPC_BOND_SYNC' in rpc
assert 'iomanX_open' not in bt and 'iomanX_write' not in bt and 'iomanX_read' not in bt, 'PADEMU still owns filesystem I/O'
assert 'TITAN_BT_CLASSIC_BOND_PATH "mass:/TITAN/BTBONDS.BIN"' in bridge
assert 'TITAN_BT_XBOX_BOND_PATH    "mass:/TITAN/XBOXBT.BIN"' in bridge
assert 'TitanStorageIo_AtomicReplace' in bridge
assert 'TitanPademuInputBridge_HasStorageWork' in bridge and 'TitanPademuInputBridge_StorageWorkerPump' in bridge
assert 'TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS' in bridge
# HasStorageWork must gate writes until pairing/HID handshake is out of CONNECT_IN_PROGRESS.
seg=bridge.split('int TitanPademuInputBridge_HasStorageWork',1)[1].split('int TitanPademuInputBridge_StorageWorkerPump',1)[0]
assert 'g_bond_storage_write_pending' in seg and 'TITAN_PADEMU_WIRELESS_CONNECT_IN_PROGRESS' in seg
assert 'TitanPademuInputBridge_StorageWorkerPump' in sched
assert 'BT_BOND_SYNC' in diag
# dirty->query->EE persist->ACK generation contract
assert 'g_bond_generation' in bt and 'g_classic_bonds_dirty' in bt and 'g_xbox_bond_dirty' in bt
assert 'g_bond_ack_requested' in bridge and 'g_bond_persist_generation' in bridge
assert re.search(r'ack_generation\s*==\s*g_bond_generation', bt)
print('b30-p105-bt-bond-storage-ownership: PASS IOP radio-only; EE storage-worker bond persistence; connect-phase write exclusion; generation ACK')
