#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
p=(root/'modules/titan-ul-engine/modules/pademu/pademu.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
tele=(root/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
rt=(root/'kernel/src/services/titan_bluetooth_runtime.c').read_text()
make=(root/'kernel/Makefile').read_text()
inputc=(root/'kernel/src/platform/ps2/titan_input.c').read_text()

# Physical P10/P10.1 evidence established two distinct failures:
#  (a) Early USB PADEMU answered fno=4 with a snapshot -> rc=-999.
#  (b) Without early.bin there was no PADEMU RPC server at all.
# BT therefore owns an independent RPC identity and an on-demand loader.
assert '#define TITAN_PADEMU_BT_TELEMETRY_RPC_ID 0x5449502Au' in tele
assert '#define TITAN_PADEMU_RPC_SERVER_ID TITAN_PADEMU_BT_TELEMETRY_RPC_ID' in rpc
assert '#define TITAN_PADEMU_RPC_SERVER_ID TITAN_PADEMU_TELEMETRY_RPC_ID' in rpc
assert 'IRX_ID("pademubt", 1, 1);' in p
assert 'if (titan_transport_only)' in p
assert 'RegisterLibraryEntries(&_exp_pademu)' in p
assert 'src/services/titan_bluetooth_runtime.o' in make
assert 'TitanBluetoothRuntime_Initialize();' in inputc
assert 'titan_bt_pademu_irx' in rt and 'SifExecModuleBuffer' in rt
assert 'TITAN_PADEMU_SETTINGS_TITAN_TRANSPORT_ONLY' in rt
assert 'TitanEarlyInput_GetStatus()' in rt
assert 'TITAN_BT_RUNTIME_READY' in rt
assert '0x0316u' in rt

# PAD_INIT must never execute in the SIF RPC server. A dedicated IOP worker
# performs USB-driver registration and the RPC returns valid initializing state.
assert 'PAD_INIT(' not in rpc
assert 'static void pademu_transport_init_worker(void *arg)' in p
worker=p[p.index('static void pademu_transport_init_worker'):p.index('int pademu_transport_request_init')]
assert 'PAD_INIT(pad_enable, pad_options)' in worker
assert 'CreateThread(&t)' in p and 'StartThread(tid, 0)' in p
assert 'PADEMU_TRANSPORT_INIT_STARTING' in p
assert 'PADEMU_TRANSPORT_INIT_READY' in p
assert 'PADEMU_TRANSPORT_INIT_FAILED' in p

wstart=rpc.index('if (fno == TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES)')
wend=rpc.index('if (fno == TITAN_PADEMU_RPC_BT_BEGIN_PAIRING)',wstart)
w=rpc[wstart:wend]
assert 'pademu_transport_request_init()' in w
assert 'pademu_transport_init_ready()' in w
assert 'g_wireless_pending = request;' in w
assert 'g_wireless_pending_valid = 1u;' in w
assert 'if (!ready)' in w
assert 'TitanPademuWirelessRequest pending = g_wireless_pending;' in w
assert 'ds34bt_wireless_command(&pending, &g_tx.wireless)' in w
assert '0x424100F2u' in w and '-710' in w

# EE keeps independent USB/BT clients and snapshots. Wireless is hard-routed
# to the BT RPC id and explicitly requests the BT runtime on user actions.
assert 'static SifRpcClientData_t g_usb_rpc' in bridge
assert 'static SifRpcClientData_t g_bt_rpc' in bridge
assert 'TITAN_PADEMU_TELEMETRY_RPC_ID,0' in bridge.replace(' ','')
assert 'TITAN_PADEMU_BT_TELEMETRY_RPC_ID,0' in bridge.replace(' ','')
assert 'sceSifCallRpc(&g_bt_rpc,TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES,SIF_RPC_M_NOWAIT' in bridge.replace(' ','')
assert 'TitanBluetoothRuntime_Request()' in bridge
assert 'g_usb_snapshot' in bridge and 'g_bt_snapshot' in bridge
assert 'g_usb_device_ids' in bridge and 'g_bt_device_ids' in bridge
assert 'TitanPademuInputBridge_BtSnapshotRpcDone' in bridge
assert 'TitanPademuInputBridge_UsbSnapshotRpcDone' in bridge

print('b30-p102-async-bt-init-rpc: PASS dedicated BT runtime/RPC + dual USB/BT snapshots + PAD_INIT off SIF RPC + retained BEGIN_SCAN')
