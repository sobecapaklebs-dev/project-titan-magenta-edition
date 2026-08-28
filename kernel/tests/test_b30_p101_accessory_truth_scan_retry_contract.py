#!/usr/bin/env python3
from pathlib import Path
import re, sys

root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
def text(p): return (root/p).read_text()
def need(c,m):
    if not c: raise AssertionError(m)

hid=text('kernel/src/services/titan_hid_input.c')
bt=text('modules/titan-ul-engine/modules/pademu/ds34bt.c')
rpc=text('modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c')
bridge=text('kernel/src/services/titan_pademu_input_bridge.c')
diag=text('kernel/src/services/titan_diagnostics.c')
scene=text('kernel/src/ui/titan_shell_scene.c')
storage=text('kernel/src/services/titan_storage_inventory.c')
scheduler=text('kernel/src/services/titan_storage_scheduler.c')

# Wired Accessories must describe physical presence, not merely loaded drivers.
init=hid[hid.index('void TitanHidInput_Initialize'):hid.index('void TitanHidInput_Update')]
need('keyboard_device_id=TitanUnifiedInput_Connect' not in init,
     'ps2kbd backend availability still fabricates a connected keyboard')
need('mouse_device_id=TitanUnifiedInput_Connect' not in init,
     'ps2mouse backend availability still fabricates a connected mouse')
need('PS2MOUSE_ENUM,SIF_RPC_M_NOWAIT' in hid,
     'mouse physical dev_count is not queried asynchronously')
need(re.search(r'count>0u\s*&&\s*g_hid\.mouse_device_id==0u[\s\S]{0,280}?TitanUnifiedInput_Connect',hid),
     'mouse registration is not gated by physical dev_count > 0')
need(re.search(r'count==0u\s*&&\s*g_hid\.mouse_device_id!=0u[\s\S]{0,220}?TitanUnifiedInput_Disconnect',hid),
     'mouse unplug is not reflected into Unified Input')
update=hid[hid.index('void TitanHidInput_Update'):hid.index('int TitanHidInput_StorageWorkerPump')]
need(re.search(r'kbd_ring_pop[\s\S]{0,420}?keyboard_device_id==0u[\s\S]{0,240}?TitanUnifiedInput_Connect',update),
     'keyboard is not registered lazily from a real report')

# Classic discovery remains queued through Pair-on-Demand HCI startup.
begin=bt[bt.index('if (command == TITAN_PADEMU_WIRELESS_BEGIN_SCAN)'):bt.index('} else if (command == TITAN_PADEMU_WIRELESS_CONNECT)')]
need('g_wireless_scan_requested = 1' in begin and 'wireless_start_classic_scan_sequence' in begin,
     'BEGIN_SCAN does not persist a queued Classic discovery request')
need('-701' not in begin,
     'normal asynchronous dongle startup is still reported as false scan error -701')
need(re.search(r'g_wireless_hci_startup_ready\s*=\s*1[\s\S]{0,180}?g_wireless_scan_requested[\s\S]{0,180}?wireless_start_classic_scan_sequence',bt),
     'queued discovery is not resumed when HCI startup is ready')

# P10.1 serializes Classic work against the already-running Xbox LE scan lane.
need('TitanBleHogp_StopScan(&xbox_ble)' in bt and 'g_wireless_wait_ble_stop = 1' in bt,
     'Classic discovery does not stop/wait for Xbox LE scanning')
need('g_wireless_wait_ble_stop = 2' in bt,
     'Classic connection does not wait for LE scan-disable completion')
need(re.search(r'hci_buf\[3\]\s*==\s*0x0c\s*&&\s*hci_buf\[4\]\s*==\s*0x20\s*&&\s*g_wireless_wait_ble_stop',bt),
     'LE Set Scan Enable completion does not release queued Classic work')
need('hci_set_titan_event_mask' in bt and 'HCI_OCF_SET_EVENT_MASK' in bt and
     'HCI_OCF_WRITE_SIMPLE_PAIRING_MODE' in bt and 'TITAN_PADEMU_WIRELESS_SSP_READY' in bt,
     'direct Classic SSP setup/event coverage was regressed')

# "Connected" means functional HID/Unified Input, never bare ACL/GATT transport.
connect_event=bt[bt.index('case HCI_EVENT_CONNECT_COMPLETE:'):bt.index('case HCI_EVENT_NUM_COMPLETED_PKT:')]
need('wireless_mark_connected' not in connect_event and 'flags |= TITAN_PADEMU_WIRELESS_CONNECTED' not in connect_event,
     'Classic ACL completion is still exposed as accessory-connected')
need(re.search(r'pad_status_set\(DS34BT_STATE_RUNNING,\s*pad\);\s*wireless_mark_connected\(ds34pad\[pad\]\.bdaddr,\s*1\)',bt),
     'Classic accessory is not marked connected after HID_RUNNING')
refresh=bt[bt.index('static void wireless_refresh_live_state'):bt.index('int ds34bt_wireless_command')]
need('if (pad_status_check(DS34BT_STATE_RUNNING, pad))' in refresh,
     'wireless snapshot does not distinguish ACL CONNECTED from HID RUNNING')
need('xbox_ble.state == TITAN_BLE_READY' in refresh and
     'xbox_ble.state == TITAN_BLE_CONNECTED || xbox_ble.state == TITAN_BLE_READY' not in refresh,
     'Xbox is user-visible connected before HOGP READY')

# Every user scan must be diagnosable despite 4Hz QUERY traffic.
need('g_wireless_request.command==TITAN_PADEMU_WIRELESS_QUERY && (g_frame%30u)==1u' in bridge,
     'QUERY traffic can still starve flight-recorder draining')
need('0x0315u' in bridge and 'WIRELESS_RPC_ACTION' in diag,
     'EE Wireless RPC action/completion diagnostic event is missing')
need('0x424100F0u' in rpc and '0x424100F1u' in rpc,
     'IOP Wireless RPC entry/exit markers are missing')
need('Inicializando Bluetooth...' in scene and 'Falha ao iniciar busca' in scene and
     'Nenhum acessório encontrado' in scene,
     'UI cannot distinguish startup/error from a completed empty inquiry')

# Keep the original P10 bounded, one-entry-per-quantum inventory model intact.
for tok in ('TITAN_STORAGE_INVENTORY_MAX_DEPTH','TITAN_STORAGE_INVENTORY_MAX_NODES',
            'TITAN_STORAGE_INVENTORY_MAX_DIRECTORY_ENTRIES'):
    need(tok in storage, tok+' missing')
need('TitanStorageIo_ListDirectory(frame->path, frame->next_index, &entry, 1u)' in storage,
     'storage inventory no longer consumes one directory entry per worker quantum')
need('TitanStorageInventory_StorageWorkerPump' in scheduler,
     'inventory is no longer pumped by Storage Scheduler')

print('b30-p101-accessory-truth-scan-retry: PASS wired physical truth + serialized Classic discovery/connect + traceability + P10 storage QoS preserved')
