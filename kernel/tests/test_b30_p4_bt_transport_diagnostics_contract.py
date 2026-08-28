#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
h=(root/'modules/titan-ul-engine/include/titan_pademu_telemetry.h').read_text()
bt=(root/'modules/titan-ul-engine/modules/pademu/ds34bt.c').read_text()
rpc=(root/'modules/titan-ul-engine/modules/pademu/pademu_telemetry_rpc.c').read_text()
bridge=(root/'kernel/src/services/titan_pademu_input_bridge.c').read_text()
ui=(root/'kernel/src/ui/titan_shell_scene.c').read_text()
host=(root/'kernel/tests/run_host_tests.sh').read_text()
assert 'TITAN_PADEMU_PAIRING_STATUS_VERSION 2u' in h
for token in ['TITAN_PADEMU_BT_STAGE_PROBE_REJECTED','TITAN_PADEMU_BT_STAGE_CONFIG_FAILED','TITAN_PADEMU_BT_STAGE_HCI_RESET_FAILED','TITAN_PADEMU_BT_STAGE_SCAN_ENABLE_FAILED','TITAN_PADEMU_BT_STAGE_READY']:
    assert token in h and token in bt
assert 'g_bt_transport.config_result = result' in bt
assert 'if (result != USB_RC_OK)' in bt
assert 'config_rc != USB_RC_OK' in bt
assert 'ds34bt_get_transport_status(&g_tx.pairing.transport)' in rpc
assert 'g_pair_rpc_buf[128]' in bridge
assert 'g_pairing.transport_stage=status.transport.stage' in bridge
assert 'TitanShellScene_BtStageName' in ui
assert '#include <titan_pademu_telemetry.h>' in ui
assert 'rc=%d usb=%02X ep=%X' in ui
assert 'sizeof(TitanPademuBluetoothPairingStatus)' in bridge
assert '-I"$PROJECT_ROOT/modules/titan-ul-engine/include"' in host
print('b30-p4-bt-transport-diagnostics: PASS USB/HCI fail-closed telemetry exposed to Controllers')
