#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(sys.argv[1] if len(sys.argv) > 1 else 'kernel')
svc = root / 'src' / 'services'
bridge = (svc / 'titan_pademu_input_bridge.c').read_text()
sched = (svc / 'titan_storage_scheduler.c').read_text()
app = (svc / 'titan_app_runtime.c').read_text()
lib = (svc / 'titan_library_scan.c').read_text()
diag = (svc / 'titan_diagnostics.c').read_text()

assert 'TITAN_PADEMU_RPC_GET_SNAPSHOT,SIF_RPC_M_NOWAIT' in bridge, 'PADEMU snapshot RPC is still blocking'
assert 'TitanPademuInputBridge_UsbSnapshotRpcDone' in bridge, 'USB PADEMU async completion callback missing'
assert 'TitanPademuInputBridge_BtSnapshotRpcDone' in bridge, 'BT PADEMU async completion callback missing'
assert 'g_usb_snapshot_rpc_pending' in bridge and 'g_usb_snapshot_rpc_done' in bridge, 'USB PADEMU async state missing'
assert 'g_bt_snapshot_rpc_pending' in bridge and 'g_bt_snapshot_rpc_done' in bridge, 'BT PADEMU async state missing'
assert 'TITAN_PADEMU_RPC_GET_SNAPSHOT,0,g_rpc_buf' not in bridge, 'blocking PADEMU snapshot call survived'

assert 'if (TitanPackageInstaller_IsActive())\n        package_budget = TITAN_STORAGE_PACKAGE_BUDGET;' in sched, 'exclusive package quantum still pressure-shrinks'
assert '4 KiB -> 1 KiB' in sched, 'package pressure feedback rationale missing'

assert '0x27F0u' in app and 'TitanTmod_RecordFlagReject' in app, 'TMOD flag rejection telemetry missing'
assert 'APP_RUNTIME' in diag and 'TMOD_FLAGS_REJECT' in diag, 'App runtime diagnostics exporter does not name new evidence'
assert 'header != 0 ? header->flags : 0u' in app, 'raw ELF flags are not recorded'

for marker in ('0x0710u', '0x0711u', '0x0712u'):
    assert marker in lib, f'Library hardware discovery telemetry {marker} missing'

print('rc1-hardware-stall-fix-contract: PASS dual async PADEMU + fixed package quantum + ABI/library evidence')
