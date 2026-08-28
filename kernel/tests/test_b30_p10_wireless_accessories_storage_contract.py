#!/usr/bin/env python3
import re
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()

def text(rel):
    p = root / rel
    assert p.is_file(), f'missing {rel}'
    return p.read_text(encoding='utf-8', errors='replace')

def need(cond, msg):
    assert cond, msg

catalog = text('kernel/src/ui/titan_shell_catalog.c')
scene = text('kernel/src/ui/titan_shell_scene.c')
wireless = text('kernel/src/services/titan_wireless_accessories.c')
wireless_h = text('kernel/src/services/titan_wireless_accessories.h')
bridge = text('kernel/src/services/titan_pademu_input_bridge.c')
storage = text('kernel/src/services/titan_storage_inventory.c')
scheduler = text('kernel/src/services/titan_storage_scheduler.c')
notifications = text('kernel/src/ui/titan_notifications.c')
loc = text('kernel/src/services/titan_localization.c')
make = text('kernel/Makefile')
telemetry = text('modules/titan-ul-engine/include/titan_pademu_telemetry.h')
bt = text('modules/titan-ul-engine/modules/pademu/ds34bt.c')
ble = text('modules/titan-ul-engine/modules/pademu/titan_ble_hogp.c')
rpc = text('modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c')

# HOME cleanup + Settings ownership.
need('"Controller"' not in catalog, 'legacy Controller tile returned to HOME catalog')
need('"Storage"' not in catalog, 'legacy Storage tile returned to HOME catalog')
need('TITAN_SETTINGS_ACCESSORIES' in scene and 'TITAN_SETTINGS_STORAGE' in scene,
     'Accessories/Storage are not Settings-owned categories')
need(re.search(r'case\s+TITAN_SETTINGS_ACCESSORIES\s*:\s*(?:\n|.){0,140}?return\s+TITAN_ICON_INPUT\s*;', scene),
     'Accessories does not reuse the input/controller glyph through the Settings icon renderer')

# Wireless UX contract: exactly the two user-facing entry rows on its root page.
wm = scene.split('static void TitanShellScene_DrawWirelessMenu',1)[1].split('static void TitanShellScene_DrawWirelessDetail',1)[0]
need('"Conectar acessório"' in wm and '"Acessórios conectados"' in wm,
     'Wireless root does not expose the two required options')
need(wm.count('TitanShellScene_DrawSettingsListRow(') == 2,
     'Wireless root gained rows beyond Connect accessory / Connected accessories')
need('TitanWirelessAccessories_BeginScan()' in scene,
     'Connect accessory page does not automatically request discovery')
need('TITAN_INPUT_TRANSPORT_BLUETOOTH' in scene and re.search(r'!=\s*TITAN_INPUT_TRANSPORT_BLUETOOTH', scene),
     'wired Accessories list does not explicitly exclude Bluetooth devices')

# Settings animation inheritance rather than a parallel transition language.
need(re.search(r'g_scene\.settings_leaf_transition\s*=\s*TitanAnimation_Approach\([\s\S]{0,180}?0\.18f', scene),
     'new Settings leaves do not use Settings transition timing')
need(re.search(r'g_scene\.settings_leaf_row_position\s*=\s*TitanAnimation_Approach\([\s\S]{0,180}?0\.24f', scene),
     'new Settings leaves do not use Settings focus timing')
need('leaf_depth * 96.0f' in scene and '(1.0f - leaf_depth) * 96.0f' in scene,
     'new Settings leaves do not inherit the Settings 96px slide composition')

# Asynchronous EE<->IOP bridge: never block the shell/frame path and never alias send/receive.
need('TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES 4' in telemetry,
     'wireless RPC ABI id changed/missing')
need('g_wireless_send_buf[64]' in bridge and 'g_wireless_rpc_buf[256]' in bridge,
     'wireless RPC does not have bounded separate send/receive buffers')
need('TitanWirelessRpcReplyFitsBuffer' in bridge,
     'wireless RPC reply no longer has a compile-time 256-byte bound')
wireless_rpc_call = bridge.split('TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES',1)[1][:550]
need('SIF_RPC_M_NOWAIT' in wireless_rpc_call,
     'Wireless Accessories RPC regressed to blocking SIF RPC')

# Direct Classic BT discovery + SSP/bonding. Keep the proven P9.2 USB provisioning fallback.
for token in ('HCI_OCF_INQUIRY', 'HCI_OCF_CREATE_CONNECTION', 'HCI_OCF_SET_EVENT_MASK',
              'HCI_OCF_WRITE_SIMPLE_PAIRING_MODE', 'HCI_EVENT_IO_CAPABILITY_REQUEST',
              'HCI_EVENT_USER_CONFIRMATION_REQUEST', 'HCI_EVENT_LINK_KEY_NOTIFICATION'):
    need(token in bt, f'missing Classic Bluetooth/SSP primitive: {token}')
need(('TITAN_CLASSIC_BOND_PATH "mass:/TITAN/BTBONDS.BIN"' in bt) or
     ('TITAN_BT_CLASSIC_BOND_PATH "mass:/TITAN/BTBONDS.BIN"' in bridge),
     'Classic Bluetooth bond database path missing')
need('0xff; hci_cmd_buf[4] = 0xff; hci_cmd_buf[5] = 0xff' not in bt or 'hci_set_titan_event_mask' in bt,
     'SSP event mask setup missing')
need('g_wireless_scan_requested' in bt and 'g_wireless_hci_startup_ready' in bt and
     'wireless_start_classic_scan_sequence' in bt,
     'scan is not deferred until HCI startup is actually ready')
need('TITAN_WIRELESS_BA(0)' in bt and 'TITAN_PADEMU_TRACE_BT_ACCESSORY_PAIRING' in bt,
     'P10 discovery/pairing flight recorder markers missing')
need('TITAN_DUALSENSE_PAIR_REPORT_ID 0x0A' in bt,
     'P9.2 Sony USB provisioning fallback was removed/regressed')
need('TitanBleHogp_StopScan' in ble and 'TitanBleHogp_StopScan' in bt,
     'Classic/BLE HCI lane arbitration is missing')
need('g_classic_bonds_dirty' in bt and
     (('wireless_flush_bonds();' in bt) or ('TITAN_PADEMU_RPC_BOND_SYNC' in telemetry)),
     'bond persistence is not deferred out of HCI callback context')

# A device is not presented as connected just because an ACL handle exists.
need(re.search(r'pad_status_set\(DS34BT_STATE_RUNNING[\s\S]{0,220}?wireless_mark_connected\([^;]+,\s*1\)', bt),
     'Wireless UI does not wait for PADEMU HID_RUNNING before marking connected')

# Device router is deliberately PADEMU/Unified Input, not a second controller stack.
need('TitanPademuInputBridge_RequestWireless' in wireless,
     'Wireless service bypasses the PADEMU bridge')
need('TITAN_PADEMU_WIRELESS_CONNECTED' in wireless,
     'Wireless service does not consume PADEMU-connected state')

# Storage inventory: bounded/background only, with existing transactional deletion paths.
need('titan_storage_inventory.o' in make and 'titan_wireless_accessories.o' in make,
     'new services are not linked into the kernel build')
need('TITAN_STORAGE_INVENTORY_MAX_DEPTH' in storage and 'TITAN_STORAGE_INVENTORY_MAX_NODES' in storage and
     'TITAN_STORAGE_INVENTORY_MAX_DIRECTORY_ENTRIES' in storage,
     'storage inventory lacks bounded traversal limits')
need('TitanStorageInventory_StorageWorkerPump' in storage and 'TitanStorageInventory_StorageWorkerPump' in scheduler,
     'storage inventory is not serviced by the storage scheduler')
storage_ui = scene[scene.index('static void TitanShellScene_DrawStorageSettings'):scene.index('static void TitanShellScene_DrawWirelessMenu')]
settings_actions = scene[scene.index('static int TitanShellScene_UpdateSettingsAction'):scene.index('static int TitanShellScene_UpdateSettingsLeaf')]
need('TitanStorageIo_' not in storage_ui and 'TitanStorageIo_' not in settings_actions,
     'new Storage Settings render/input path performs filesystem I/O directly')
need('TitanStorageScheduler_RequestApplicationUninstall' in scene,
     'app/native-game deletion bypasses existing transactional uninstall path')
need('TitanTheme_CanDelete' in scene and 'TitanTheme_Delete' in scene,
     'theme deletion bypasses theme protection/delete service')

# Neutral information modal, separate from error presentation.
need('TitanNotifications_OpenInformation' in notifications and 'information' in notifications,
     'information modal missing')
info = notifications.split('if (g_modal.information)',1)[1] if 'if (g_modal.information)' in notifications else notifications
need('70' in info and '132' in info and '220' in info,
     'information modal does not retain its neutral information accent')
need('\"Accessories\"' in loc and '\"Storage\"' in loc and '\"Acessórios\"' in loc and '\"Armazenamento\"' in loc,
     'new Settings categories are not in localization service')

# IOP RPC actually exports command 4.
need('TITAN_PADEMU_RPC_WIRELESS_ACCESSORIES' in rpc and 'ds34bt_wireless_command' in rpc,
     'IOP RPC server does not expose Wireless Accessories command')

print('b30-p10-wireless-accessories-storage: PASS Settings-native UX + direct BT/SSP/bond + PADEMU route + bounded storage inventory')
