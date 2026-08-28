#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[2])
def req(c,m):
    if not c: raise SystemExit('FAIL: '+m)
mk=(root/'kernel/Makefile').read_text()
k=(root/'kernel/src/core/titan_kernel.c').read_text()
e=(root/'kernel/src/services/titan_early_input.c').read_text()
p=(root/'modules/titan-ul-engine/modules/pademu/pademu.c').read_text()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
imp=(root/'modules/titan-ul-engine/modules/pademu/imports.lst').read_text()
for t in ['TITAN_USB_PADEMU_IRX','TITAN_BT_PADEMU_IRX','TITAN_SMSUTILS_IRX']:
    req(t in mk,t+' build hook missing')
req('TitanEarlyInput_Initialize();' in k,'Early Input not wired before runtime loop')
req(k.index('TitanEarlyInput_Initialize();') < k.index('for (;;)'),'Early Input starts after Health/runtime loop')
for t in ['TITAN_EARLY_INPUT_CONFIG_PATH','TITAN_EARLY_INPUT_BACKEND_BT','titan_bt_pademu_irx','titan_usb_pademu_irx']:
    req(t in e,t+' missing')
req('return TITAN_RESULT_OK' in e,'modern backend failure not fail-safe')
req('ds34bt_set_xbox_ble_bond' in p and 'TITAN_PADEMU_BOOT_EXT_MAGIC' in p,'bond not injected before PADEMU lazy init')
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
sched=(root/'kernel/src/services/titan_storage_scheduler.c').read_text()
abi=(root/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
req(
    'mass:/TITAN/XBOXBT.BIN' in bridge
    and 'TITAN_PADEMU_RPC_BOND_SYNC' in abi
    and 'TitanStorageIo_AtomicReplace' in bridge
    and 'TitanPademuInputBridge_StorageWorkerPump' in sched,
    'Xbox bond is not persistent across boot through EE storage ownership'
)
req('iomanX_open(' not in bt and 'iomanX_read(' not in bt and 'iomanX_write(' not in bt,
    'PADEMU still performs bond filesystem I/O on the USBD/HCI owner')
req('I_open' not in imp and 'I_read' not in imp and 'I_write' not in imp,
    'obsolete PADEMU iomanX bond imports remain linked')
print('buildfix28-xbox-early-input-contract: OK')
