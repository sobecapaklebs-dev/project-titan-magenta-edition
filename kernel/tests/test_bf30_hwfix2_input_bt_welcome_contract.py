#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
scene=(r/'kernel/src/ui/titan_shell_scene.c').read_text()
bridge=(r/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
early=(r/'kernel/src/services/titan_early_input.c').read_text()
unified=(r/'kernel/src/services/titan_unified_input.c').read_text()
platform_input=(r/'kernel/src/platform/ps2/titan_input.c').read_text()
tele=(r/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
rpc=(r/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
bt=(r/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
checks={
 'controllers-operational-page':'Pair wireless controller' in scene and 'Startup input backend' in scene,
 'backend-persistent':'TitanEarlyInput_SaveBackendPreference' in early and 'early.bin.tmp' in early and 'TitanStorageIo_AtomicReplace' in early,
 'pair-rpc-wire':'TITAN_PADEMU_RPC_BT_BEGIN_PAIRING' in tele and 'TITAN_PADEMU_PAIRING_STATUS_VERSION' in tele,
 'pair-rpc-server':'ds34bt_begin_pairing()' in rpc and 'ds34bt_pairing_available()' in rpc,
 'classic-and-ble-listen':'hci_write_scan_enable(SCAN_ENABLE_NOINQ_ENPAG)' in bt and 'TitanBleHogp_StartScan(&xbox_ble)' in bt,
 'system-latch':'TitanPademuInputBridge_SystemButtonHandler' in bridge and 'g_system_latched[port]' in bridge and 'SifAddCmdHandler' in bridge,
 'physical-ds2-preserved':('TitanUnifiedInput_SubmitLegacyPad(port, g_pads[port].connected' in platform_input and
                           'PADEMU transport-only devices' in platform_input and
                           'TitanUnifiedInput_SubmitLegacyPad(port,0' not in bridge),
 'welcome-held-or-edge':'d->state.pressed|d->state.buttons' in scene.replace(' ',''),
 'boot-ds2-first-input':'!g_boot_ui_active && physical_port' in unified,
}
failed=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(f'bf30-hwfix2-input: {"PASS" if v else "FAIL"} {k}')
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'bf30-hwfix2-input: PASS {len(checks)}/{len(checks)}')
